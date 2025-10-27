#pragma once

#include <string>

#include "legacy_embedded_ctl/assert.h"

namespace celonis::accelerator {
class MemoryThreshold;  // forward declare for MemoryThreshold in operators.pb.h
namespace memory::management {

class memory_threshold final {
 public:
  constexpr memory_threshold() noexcept = default;
  constexpr memory_threshold(double lower, double higher) noexcept { safe_set(lower, higher); }
  [[nodiscard]] constexpr double lower() const noexcept { return lower_threshold; }
  [[nodiscard]] constexpr double higher() const noexcept { return higher_threshold; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return is_valid(lower(), higher()); }
  constexpr void safe_set(double lower, double higher) noexcept {
    if (is_valid(lower, higher)) {
      set(lower, higher);
    }
  }
  constexpr void set(double lower, double higher) noexcept {
    legacy_embedded_debug_assert(is_valid(lower, higher));
    set_lower(lower);
    set_higher(higher);
  }
#ifndef CELOSTAR
  void set(const MemoryThreshold& protobuf_memory_threshold);
#endif
  constexpr void set_lower(double lower) noexcept {
    legacy_embedded_debug_assert(is_valid(lower, higher_threshold));
    lower_threshold = lower;
  }
  constexpr void set_higher(double higher) noexcept {
    legacy_embedded_debug_assert(is_valid(lower_threshold, higher));
    higher_threshold = higher;
  }

 private:
  [[nodiscard]] static constexpr bool is_valid(const double lower, const double higher) noexcept {
    return 0 <= lower && lower <= higher && higher <= 1;
  }
  double lower_threshold{1.0};
  double higher_threshold{1.0};
};

}  // namespace memory::management
}  // namespace celonis::accelerator
