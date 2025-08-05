#pragma once

#include <cstdint>

#include "legacy_embedded_ctl/type_traits.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/* Return type for combined division and modulo operation */
template <typename T>
struct div_mod_result {
  static_assert(is_standard_integer_v<T>);
  T quotient;
  T rest;
};

/* Euclidean division and modulo */
[[nodiscard]] constexpr div_mod_result<std::int64_t> euclidean_division_modulo(const std::int64_t dividend,
                                                                               const std::int64_t divisor) noexcept {
  auto rest{dividend % divisor};
  auto quotient{dividend / divisor};
  if (rest < 0) {
    if (divisor > 0) {
      --quotient;
      rest = rest + divisor;
    } else {
      ++quotient;
      rest = rest - divisor;
    }
  }
  return {quotient, rest};
}

/* Discrete logarithm with base 2
 * Input <value> must not be negative, otherwise behavior is undefined */
[[nodiscard]] constexpr std::int32_t discrete_log2(std::int64_t value) noexcept {
  std::int32_t ret{0};
  while (value != 0) {
    ++ret;
    value /= 2;
  }
  return ret;
}

/** Integer division which rounds up instead of down (e.g. div_round_up(3, 2) == 2) */
template <typename T>
[[nodiscard]] constexpr T div_round_up(T num, T den) noexcept {
  return num / den + static_cast<T>(num % den != 0);
}

/* Add enough space to a size to ensure that the resulting size is 8 byte aligned */
[[nodiscard]] constexpr size_t get_8_byte_aligned_size(size_t required_size) noexcept {
  auto rest = required_size % 8;
  auto padding = rest == 0 ? 0 : 8 - rest;
  return required_size + padding;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
