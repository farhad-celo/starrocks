#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "ctl/type_traits.h"

namespace celonis::accelerator::ctl {

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

}  // namespace celonis::accelerator::ctl
