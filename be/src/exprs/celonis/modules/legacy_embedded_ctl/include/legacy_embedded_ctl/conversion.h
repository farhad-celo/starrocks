#pragma once

#include <optional>
#include <type_traits>

#include "legacy_embedded_ctl/exception.h"
#include "legacy_embedded_ctl/type_traits.h"
#include "legacy_embedded_ctl/utils/type_utils.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Proxy for `std::in_range` with an arguably more explicit naming.
 */
template <typename TARGET_TYPE, typename SOURCE_TYPE>
[[nodiscard]] constexpr bool is_safe_to_cast(const SOURCE_TYPE source_value) noexcept {
  return std::in_range<TARGET_TYPE>(source_value);
}

/**
 * @brief Safer cast than 'static_cast' for numeric values. Checks that the source value fits into the target type.
 * @return Optional containing the casted value with the given target type if the cast worked, 'std::nullopt' otherwise
 */
template <typename TARGET_TYPE, typename SOURCE_TYPE>
[[nodiscard]] constexpr std::optional<TARGET_TYPE> try_cast(const SOURCE_TYPE source_value) noexcept {
  return is_safe_to_cast<TARGET_TYPE>(source_value)
             ? std::make_optional<TARGET_TYPE>(static_cast<TARGET_TYPE>(source_value))
             : std::nullopt;
}

/**
 * @brief Throwing version of 'try_cast'.
 * @return The casted value with the given target type if the cast worked
 * @throws conversion_exception if the cast did not work (i.e., the source value does not fit into the target type)
 */
template <typename TARGET_TYPE, typename SOURCE_TYPE>
[[nodiscard]] constexpr TARGET_TYPE cast(const SOURCE_TYPE source_value) {
  if (is_safe_to_cast<TARGET_TYPE>(source_value)) {
    return static_cast<TARGET_TYPE>(source_value);
  }
  throw conversion_exception{source_value, utils::type_name<SOURCE_TYPE>(), utils::type_name<TARGET_TYPE>()};
}

/**
 * @brief Safely tries to cast the given numeric source value into an unsigned representation.
 * @return Optional containing the casted unsigned value representation if the cast worked, 'std::nullopt' otherwise
 */
template <typename SOURCE_TYPE>
[[nodiscard]] constexpr std::optional<std::make_unsigned_t<SOURCE_TYPE>> try_cast_unsigned(
    const SOURCE_TYPE source_value) noexcept {
  return try_cast<std::make_unsigned_t<SOURCE_TYPE>>(source_value);
}

/**
 * @brief Throwing version of 'try_cast_unsigned'.
 * @return The casted unsigned value representation if the cast worked
 * @throws conversion_exception if the cast did not work (i.e., the value cannot be represented in its unsigned type)
 */
template <typename SOURCE_TYPE>
[[nodiscard]] constexpr std::make_unsigned_t<SOURCE_TYPE> cast_unsigned(const SOURCE_TYPE source_value) {
  return cast<std::make_unsigned_t<SOURCE_TYPE>>(source_value);
}

/**
 * @brief Safely tries to cast the given numeric source value into a signed representation.
 * @return Optional containing the casted signed value representation if the cast worked, 'std::nullopt' otherwise
 */
template <typename SOURCE_TYPE>
[[nodiscard]] constexpr std::optional<std::make_signed_t<SOURCE_TYPE>> try_cast_signed(
    const SOURCE_TYPE source_value) noexcept {
  return try_cast<std::make_signed_t<SOURCE_TYPE>>(source_value);
}

/**
 * @brief Throwing version of 'try_cast_signed'.
 * @return The casted signed value representation if the cast worked
 * @throws conversion_exception if the cast did not work (i.e., the value cannot be represented in its signed type)
 */
template <typename SOURCE_TYPE>
[[nodiscard]] constexpr std::make_signed_t<SOURCE_TYPE> cast_signed(const SOURCE_TYPE source_value) {
  return cast<std::make_signed_t<SOURCE_TYPE>>(source_value);
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
