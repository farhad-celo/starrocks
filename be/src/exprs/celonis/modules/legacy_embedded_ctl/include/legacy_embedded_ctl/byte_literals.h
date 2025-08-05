#pragma once

#include <cstddef>

#include "legacy_embedded_ctl/type_traits.h"

namespace celonis::accelerator::legacy_embedded_ctl {

static constexpr std::size_t BYTES_PER_KiB = 1024;
static constexpr std::size_t BYTES_PER_MiB = BYTES_PER_KiB * 1024;
static constexpr std::size_t BYTES_PER_GiB = BYTES_PER_MiB * 1024;
static constexpr std::size_t BYTES_PER_TiB = BYTES_PER_GiB * 1024;

static constexpr std::size_t BYTES_PER_KB = 1000;
static constexpr std::size_t BYTES_PER_MB = BYTES_PER_KB * 1000;
static constexpr std::size_t BYTES_PER_GB = BYTES_PER_MB * 1000;
static constexpr std::size_t BYTES_PER_TB = BYTES_PER_GB * 1000;

/**
 * @brief represents the units for common binary and decimal multiples of a byte
 */
enum class byte_unit { B, KB, KiB, MB, MiB, GB, GiB, TB, TiB };

/**
 * @brief returns the number of bytes contained in the given byte unit
 * @tparam BYTE_UNIT an enum representing the byte unit for which the number of bytes shall be returned
 */
template <byte_unit BYTE_UNIT>
[[nodiscard]] constexpr std::size_t bytes_in_unit() noexcept {
  if constexpr (BYTE_UNIT == byte_unit::B) {
    return 1;
  } else if constexpr (BYTE_UNIT == byte_unit::KB) {
    return BYTES_PER_KB;
  } else if constexpr (BYTE_UNIT == byte_unit::KiB) {
    return BYTES_PER_KiB;
  } else if constexpr (BYTE_UNIT == byte_unit::MB) {
    return BYTES_PER_MB;
  } else if constexpr (BYTE_UNIT == byte_unit::MiB) {
    return BYTES_PER_MiB;
  } else if constexpr (BYTE_UNIT == byte_unit::GB) {
    return BYTES_PER_GB;
  } else if constexpr (BYTE_UNIT == byte_unit::GiB) {
    return BYTES_PER_GiB;
  } else if constexpr (BYTE_UNIT == byte_unit::TB) {
    return BYTES_PER_TB;
  } else if constexpr (BYTE_UNIT == byte_unit::TiB) {
    return BYTES_PER_TiB;
  } else {
    static_assert(always_false_v<decltype(BYTE_UNIT)>, "Unknown byte unit.");
  }
}

/**
 * @brief converts a given number of bytes from a given source unit to the given target unit
 * @tparam SOURCE_BYTE_UNIT an enum representing the source byte unit which we want to convert
 * @tparam TARGET_BYTE_UNIT an enum representing the target byte unit for the conversion
 * @param size the number of bytes with the unit 'SOURCE_BYTE_UNIT'
 * @return the converted number of bytes from 'SOURCE_BYTE_UNIT' to 'TARGET_BYTE_UNIT'
 */
template <byte_unit SOURCE_BYTE_UNIT, byte_unit TARGET_BYTE_UNIT>
[[nodiscard]] constexpr std::size_t convert_byte_size(const std::size_t size) noexcept {
  if constexpr (SOURCE_BYTE_UNIT == TARGET_BYTE_UNIT) {
    return size;
  }

  const std::size_t size_in_bytes = size * bytes_in_unit<SOURCE_BYTE_UNIT>();
  return size_in_bytes / bytes_in_unit<TARGET_BYTE_UNIT>();
}

}  // namespace celonis::accelerator::legacy_embedded_ctl

/**
 * @note the provided literals will probably moved to a more central place (e.g., "literals.h") later
 * on as soon as other allocator related code has been moved to the CTL
 */
namespace celonis::accelerator {

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_KB(const unsigned long long kilobyte) noexcept -> std::size_t {
  return kilobyte * legacy_embedded_ctl::BYTES_PER_KB;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_KiB(const unsigned long long kibibyte) noexcept -> std::size_t {
  return kibibyte * legacy_embedded_ctl::BYTES_PER_KiB;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_MB(const unsigned long long megabyte) noexcept -> std::size_t {
  return megabyte * legacy_embedded_ctl::BYTES_PER_MB;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_MiB(const unsigned long long mebibyte) noexcept -> std::size_t {
  return mebibyte * legacy_embedded_ctl::BYTES_PER_MiB;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_GB(const unsigned long long gigabyte) noexcept -> std::size_t {
  return gigabyte * legacy_embedded_ctl::BYTES_PER_GB;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_GiB(const unsigned long long gibibyte) noexcept -> std::size_t {
  return gibibyte * legacy_embedded_ctl::BYTES_PER_GiB;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_TB(const unsigned long long terabyte) noexcept -> std::size_t {
  return terabyte * legacy_embedded_ctl::BYTES_PER_TB;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_TiB(const unsigned long long tebibyte) noexcept -> std::size_t {
  return tebibyte * legacy_embedded_ctl::BYTES_PER_TiB;
}

}  // namespace celonis::accelerator
