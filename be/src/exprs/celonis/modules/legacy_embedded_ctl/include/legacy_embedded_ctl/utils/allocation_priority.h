#pragma once

#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace celonis::accelerator::legacy_embedded_ctl::utils {

/**
 * Represents a priority for an allocation request. The higher the priority, the more likely it will be fulfilled during
 * high memory pressure.
 * Note: The vast majority of allocation factories use a default value of 'LOW'. This should generally not be
 * overwritten except the allocation is indeed critical (e.g., to allocate memory to report an error) and in the best
 * case also short lived (to quickly release the additional memory pressure).
 */
enum class allocation_priority : std::int8_t { LOW, MEDIUM, HIGH, EMERGENCY };

[[nodiscard]] inline std::string to_string(const allocation_priority priority) {
  switch (priority) {
    case allocation_priority::LOW:
      return "LOW";
    case allocation_priority::MEDIUM:
      return "MEDIUM";
    case allocation_priority::HIGH:
      return "HIGH";
    case allocation_priority::EMERGENCY:
      return "EMERGENCY";
    default:
      return "UNKNOWN_PRIORITY";
  }
}

}  // namespace celonis::accelerator::legacy_embedded_ctl::utils

template <>
struct fmt::formatter<celonis::accelerator::legacy_embedded_ctl::utils::allocation_priority> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  /** Custom {fmt} formatter for an allocation priority. */
  template <typename FormatContext>
  auto format(const celonis::accelerator::legacy_embedded_ctl::utils::allocation_priority& priority,
              FormatContext& ctx) {
    return fmt::format_to(ctx.out(), "{0}", celonis::accelerator::legacy_embedded_ctl::utils::to_string(priority));
  }
};
