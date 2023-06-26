#pragma once

#include <string_view>
#include <type_traits>

#include "modules/common/int_types.h"
#include "types/uuid/uuid_storage.h"

namespace celonis::accelerator {
namespace date {
class celonis_date_storage;
}  // namespace date
/**
 * Activity count data type.
 * Only use this data type if check that less than 2^15 activities are used.
 * That should be true for most use cases.
 */
using activity_short = int16_t;
/**
 * this is int due to protobuf max number of arrays size */
using cel_column_count_t = int;
/**
 * Integer column type.
 */
using cel_int_t = int64_t;
/**
 * Floating point column type.
 */
using cel_float_t = double;
/**
 * String column type.
 */
using cel_string_t = const char*;
using cel_mut_string_t = char*;
/**
 * Date column type.
 */
using cel_date_t = date::celonis_date_storage;
/**
 * Boolean column type.
 * Only used for temporaries.
 */
using cel_boolean_t = bool;
/**
 * UUID column type.
 */
using cel_uuid_t = types::uuid::uuid_storage;
/**
 * Null column type.
 * Exactly one distinct value of this type exists: cel_null_v
 */
struct cel_null_t {
  static constexpr std::string_view TEXT_REPRESENTATION{"NULL"};
};
constexpr cel_null_t cel_null_v{};

template <typename LHS, typename RHS>
constexpr bool is_same_underlying_type() noexcept {
  using LHS_UNDERLYING_TYPE = std::remove_cv_t<std::remove_reference_t<LHS>>;
  using RHS_UNDERLYING_TYPE = std::remove_cv_t<std::remove_reference_t<RHS>>;
  return std::is_same_v<LHS_UNDERLYING_TYPE, RHS_UNDERLYING_TYPE>;
}

template <typename T>
constexpr bool is_supported_type() noexcept {
  return is_same_underlying_type<T, cel_int_t>() || is_same_underlying_type<T, cel_float_t>() ||
         is_same_underlying_type<T, cel_date_t>() || is_same_underlying_type<T, cel_string_t>() ||
         is_same_underlying_type<T, cel_boolean_t>() || is_same_underlying_type<T, cel_null_t>() ||
         is_same_underlying_type<T, cel_uuid_t>();
}

enum class data_type : int;

enum class col_pointer_type : int;

}  // namespace celonis::accelerator
