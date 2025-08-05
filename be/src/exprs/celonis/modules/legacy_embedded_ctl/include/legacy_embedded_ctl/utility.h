#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "legacy_embedded_ctl/type_traits.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * Helper type for std::visit usage.
 * Taken from https://en.cppreference.com/w/cpp/utility/variant/visit (see example of visitor #4)
 */
template <typename... Ts>
struct overloaded final : Ts... {
  constexpr overloaded() noexcept((... && std::is_nothrow_default_constructible_v<Ts>)) = default;
  constexpr explicit overloaded(Ts&&... ts) noexcept((... && std::is_nothrow_constructible_v<Ts, Ts&&>))
      : Ts(std::move(ts))... {}
  constexpr explicit overloaded(const Ts&... ts) noexcept((... && std::is_nothrow_constructible_v<Ts, Ts&>))
      : Ts(ts)... {}
  using Ts::operator()...;
};

template <typename CALLABLE>
[[nodiscard]] auto execute_with_retry(const CALLABLE& callback, const std::size_t max_attempts,
                                      const std::chrono::milliseconds time_between_retries_ms) {
  static_assert(std::is_invocable_v<decltype(callback)>, "Callable can not be invoked (or requires call arguments).");
  static_assert(!std::is_void_v<std::invoke_result_t<decltype(callback)>>,
                "Callable must have a non-void return type.");
  if (max_attempts == 0) {
    throw std::domain_error{"The given number of attempts is zero."};
  }
  const auto max_retries{max_attempts - 1};
  for (std::size_t retry{0}; retry < max_retries; ++retry) {
    try {
      return callback();
    } catch (...) {
      std::this_thread::sleep_for(time_between_retries_ms);
    }
  }
  return callback();
}

/**
 * @brief Simply stores the given pointer in an integer. The integer can be safely transformed into a pointer again.
 */
[[nodiscard]] inline std::intptr_t ptr_to_int(const void* const pointer) noexcept {
  return reinterpret_cast<std::intptr_t>(pointer);
}

template <typename T>
using optional_ref = std::optional<std::reference_wrapper<T>>;

template <typename T>
[[nodiscard]] optional_ref<T> element_or_null(std::shared_future<T>& future) {
  try {
    return std::reference_wrapper<T>{future.get()};
  } catch (...) {
    return std::nullopt;
  }
}

template <typename T>
[[nodiscard]] optional_ref<const T> element_or_null(const std::shared_future<T>& future) {
  try {
    return std::reference_wrapper<const T>{future.get()};
  } catch (...) {
    return std::nullopt;
  }
}

template <typename T>
[[nodiscard]] bool is_subspan(std::span<T> outer, std::span<T> inner) {
  std::less_equal<T*> leq{};
  return leq(outer.data(), inner.data()) && leq(inner.data() + inner.size(), outer.data() + outer.size());
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
