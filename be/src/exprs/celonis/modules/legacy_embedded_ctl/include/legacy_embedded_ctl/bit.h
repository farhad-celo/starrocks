#pragma once

#include <bit>
#include <climits>
#include <cstddef>
#include <type_traits>

#include "legacy_embedded_ctl/assert.h"

/**
 * @brief A few frequently used and useful bit operations
 * @note More useful "bit twiddling hacks": http://graphics.stanford.edu/~seander/bithacks.html
 */
namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Returns the bit width for the given type
 */
template <typename T>
[[nodiscard]] constexpr std::size_t bit_width_for_type() noexcept;

/**
 * @brief Returns whether the given integer is a power of two
 */
template <typename T>
[[nodiscard]] constexpr bool is_power_of_two(T i) noexcept;

/**
 * @brief Finds the (zero based) bit index of the first (least significant) set bit within the given integer
 * @precondition the given integer MUST NOT be 0 (only checked via assertion); undefined behaviour otherwise
 */
template <typename T>
[[nodiscard]] int find_first_set(T i) noexcept;

/** Implementations */

template <typename T>
constexpr std::size_t bit_width_for_type() noexcept {
  return sizeof(T) * CHAR_BIT;
}

template <typename T>
constexpr bool is_power_of_two(const T i) noexcept {
  return std::has_single_bit(i);
}

template <typename T>
int find_first_set(T i) noexcept {
  static_assert(std::is_unsigned_v<T>, "find_first_set is only defined for unsigned integer types.");
  legacy_embedded_debug_assert(i != 0);
  return std::countr_zero(i);
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
