#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "legacy_embedded_ctl/source_location.h"

namespace celonis::accelerator::legacy_embedded_ctl::utils {

/**
 * @brief This class represents an allocation reason.
 *
 * It is usually created and provided to the memory allocation factories for providing context in case the allocation
 * fails. It contains an allocation message with a description of the allocation reason and a source location from where
 * the memory allocation was requested.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
class allocation_reason {
 public:
  constexpr allocation_reason(legacy_embedded_ctl::source_location source_location, const std::string_view allocation_message) noexcept
      : source_location_{source_location}, allocation_message_{allocation_message} {}

  [[nodiscard]] constexpr const legacy_embedded_ctl::source_location& source_location() const& noexcept { return source_location_; }

  /**
   * @brief Returns a formatted string of the allocation reason.
   *
   * It contains the allocation message and the formatted source location (in Release builds the file name is hashed).
   */
  [[nodiscard]] virtual std::string to_string() const {
    return fmt::format("Allocation Reason: '{}' at {}", allocation_message_, source_location_);
  }

  /**
   * @brief delete the standard and array operator new to prevent objects from being directly allocated on the heap
   * @note this also hides the other operator new overloads (e.g., placement new or aligned new)
   */
  static void* operator new(std::size_t) = delete;    // standard new
  static void* operator new[](std::size_t) = delete;  // array new

 private:
  legacy_embedded_ctl::source_location source_location_;
  std::string_view allocation_message_;
};

template <typename... ENRICHING_MESSAGES>
class allocation_reason_enriched final : public allocation_reason {
 public:
  template <typename... ARGS>
  constexpr allocation_reason_enriched(legacy_embedded_ctl::source_location source_location, std::string_view allocation_message,
                                       ARGS&&... messages)
      : allocation_reason{source_location, allocation_message}, enriching_messages_{std::forward<ARGS>(messages)...} {}

  /**
   * @brief Returns a formatted string of the enriched allocation reason.
   */
  [[nodiscard]] std::string to_string() const override {
    return fmt::format("{} ({})", allocation_reason::to_string(), fmt::join(enriching_messages_, ", "));
  }

  /**
   * @brief delete the standard and array operator new to prevent objects from being directly allocated on the heap
   * @note this also hides the other operator new overloads (e.g., placement new or aligned new)
   */
  static void* operator new(std::size_t) = delete;    // standard new
  static void* operator new[](std::size_t) = delete;  // array new

 private:
  std::tuple<ENRICHING_MESSAGES...> enriching_messages_;
  static_assert(std::tuple_size_v<decltype(enriching_messages_)> > 0,
                "Use allocation_reason base if no enriching messages are provided.");
};
#pragma GCC diagnostic pop

template <typename... ARGS>
[[nodiscard]] constexpr auto make_allocation_reason(source_location source_location,
                                                    const std::string_view allocation_message, ARGS&&... args) {
  if constexpr (sizeof...(ARGS) == 0) {
    return allocation_reason{source_location, allocation_message};
  } else {
    return allocation_reason_enriched<ARGS...>{source_location, allocation_message, std::forward<ARGS>(args)...};
  }
}

}  // namespace celonis::accelerator::legacy_embedded_ctl::utils

/**
 * @brief As long as not all compilers support std::source_location::current() this macro makes using allocation reasons
 * more convenient to use (i.e., automatically injects the source location at the callers side)
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LEGACY_EMBEDDED_ALLOC_MSG(...) \
  (celonis::accelerator::legacy_embedded_ctl::utils::make_allocation_reason(celonis::accelerator::legacy_embedded_ctl::source_location{}, __VA_ARGS__))
