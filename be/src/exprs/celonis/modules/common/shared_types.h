#pragma once

#include <array>
#include <cmath>
#include <ostream>
#include <string>
#include <type_traits>

#include <fmt/ostream.h>

#include "legacy_embedded_ctl/type_traits.h"
#include "modules/common/int_types.h"
#include "modules/common/shared_types_fwd.h"

namespace celonis::accelerator {

[[nodiscard]] constexpr size_t hash_value(const cel_null_t& /*null*/) noexcept { return 0; }

[[nodiscard]] constexpr bool operator==(cel_null_t /*lhs*/, cel_null_t /*rhs*/) noexcept { return true; }
[[nodiscard]] constexpr bool operator!=(cel_null_t /*lhs*/, cel_null_t /*rhs*/) noexcept { return false; }
[[nodiscard]] constexpr bool operator<(cel_null_t /*lhs*/, cel_null_t /*rhs*/) noexcept { return false; }
[[nodiscard]] constexpr bool operator<=(cel_null_t /*lhs*/, cel_null_t /*rhs*/) noexcept { return true; }
[[nodiscard]] constexpr bool operator>(cel_null_t /*lhs*/, cel_null_t /*rhs*/) noexcept { return false; }
[[nodiscard]] constexpr bool operator>=(cel_null_t /*lhs*/, cel_null_t /*rhs*/) noexcept { return true; }

std::ostream& operator<<(std::ostream& os, cel_null_t null);

enum class data_type : int {
  cel_int,      // NOLINT(readability-identifier-naming)
  cel_string,   // NOLINT(readability-identifier-naming)
  cel_float,    // NOLINT(readability-identifier-naming)
  cel_date,     // NOLINT(readability-identifier-naming)
  cel_boolean,  // NOLINT(readability-identifier-naming)
  cel_uuid,     // NOLINT(readability-identifier-naming)
  cel_null      // NOLINT(readability-identifier-naming)
};

// The data_type enum values should be accessible without the class name
using enum data_type;

std::ostream& operator<<(std::ostream& os, data_type type);

// Pointer type (bit size)
enum class col_pointer_type : int { PTR_8 = 1, PTR_16 = 2, PTR_32 = 3, PTR_64 = 4 };

template <typename T>
constexpr data_type get_matching_data_type() noexcept {
  static_assert(is_supported_type<T>(), "Unknown data type.");
  if constexpr (is_same_underlying_type<T, cel_int_t>()) {
    return data_type::cel_int;
  } else if constexpr (is_same_underlying_type<T, cel_float_t>()) {
    return data_type::cel_float;
  } else if constexpr (is_same_underlying_type<T, cel_date_t>()) {
    return data_type::cel_date;
  } else if constexpr (is_same_underlying_type<T, cel_string_t>()) {
    return data_type::cel_string;
  } else if constexpr (is_same_underlying_type<T, cel_boolean_t>()) {
    return data_type::cel_boolean;
  } else if constexpr (is_same_underlying_type<T, cel_uuid_t>()) {
    return data_type::cel_uuid;
  } else if constexpr (is_same_underlying_type<T, cel_null_t>()) {
    return data_type::cel_null;
  } else {
    static_assert(legacy_embedded_ctl::always_false_v<T>, "The type has no matching data type.");
  }
}

data_type convert_from_string(const std::string& type);
std::string convert_to_string(data_type celonis_data_type);

[[nodiscard]] bool is_cel_int_value(cel_float_t value) noexcept;

// Both NULL_STRING itself and calling size() on it includes null-termination
static constexpr std::array<const char, 5> NULL_STRING{"NULL"};
static_assert(NULL_STRING.size() == 5, "NULL_STRING size including null-termination");
static_assert(NULL_STRING.back() == '\0', "NULL_STRING null-termination");

// this is int due to protobuf max number of arrays size
using cel_table_column_count_type = int;

}  // namespace celonis::accelerator

namespace std {

template <>
struct hash<celonis::accelerator::cel_null_t> {
  constexpr size_t operator()(const celonis::accelerator::cel_null_t& null) const noexcept {
    return celonis::accelerator::hash_value(null);
  }
};

}  // namespace std
