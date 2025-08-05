#pragma once

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "legacy_embedded_ctl/type_traits.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/shared_types.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/column.h"
#include "modules/memory/column_iterable.h"

namespace celonis::accelerator::utils {

template <typename T>
using owned_cel_type_t = std::conditional_t<std::is_same_v<T, cel_string_t>, std::string, T>;

/**
 * A wrapper around the pql types which also supports null values. Effectively an std::optional<type>.
 *
 * NB: For cel_string_t, this class represents an *owned* version of the string. I.e. it does not store a pointer, but
 * an std::string internally.
 */
template <typename T>
class nullable_pql_value {
  static_assert(std::is_same_v<T, cel_int_t> || std::is_same_v<T, cel_float_t> || std::is_same_v<T, cel_boolean_t> ||
                    std::is_same_v<T, cel_date_t> || std::is_same_v<T, cel_string_t> || std::is_same_v<T, cel_uuid_t>,
                "Not supported type");

 public:
  using value_type = T;
  constexpr nullable_pql_value() = default;
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr nullable_pql_value(cel_null_t /**/) noexcept : nullable_value_{std::nullopt} {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr nullable_pql_value(T value) : nullable_value_{std::make_optional(owned_cel_type_t<T>{value})} {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr nullable_pql_value(std::optional<T> value)
      : nullable_value_{value.has_value() ? std::make_optional(owned_cel_type_t<T>{value.value()}) : std::nullopt} {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  nullable_pql_value(std::string value) requires(std::is_same_v<T, cel_string_t>) : nullable_value_{std::move(value)} {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  nullable_pql_value(std::string_view value) requires(std::is_same_v<T, cel_string_t>)
      : nullable_value_{std::string{value}} {}

  /** Checks if the value is not null */
  [[nodiscard]] constexpr bool has_value() const noexcept { return nullable_value_.has_value(); }
  /** Checks if the value is null */
  [[nodiscard]] constexpr bool is_null() const noexcept { return !nullable_value_.has_value(); }

  /** If the value is null, returns the provided default value. Otherwise, returns the actual value */
  [[nodiscard]] constexpr const owned_cel_type_t<T>& get_or_default(const T& def = T{}) const {
    if (is_null()) {
      return def;
    }
    return nullable_value_.value();
  }

  /** If the value is null, throws an exception. Otherwise, returns the actual value */
  [[nodiscard]] constexpr const owned_cel_type_t<T>& get_or_throw() const {
    if (is_null()) {
      throw common::internal_exception{"value is null"};
    }
    return nullable_value_.value();
  }

  [[nodiscard]] constexpr bool operator<=>(const nullable_pql_value<T>& other) const = default;
  [[nodiscard]] constexpr bool operator==(const nullable_pql_value<T>& other) const = default;
  [[nodiscard]] constexpr bool operator==(cel_null_t /**/) const noexcept { return is_null(); }
  [[nodiscard]] constexpr bool operator==(const std::optional<T>& other) const { return nullable_value_ == other; }
  [[nodiscard]] constexpr bool operator==(T other) const { return !is_null() && *nullable_value_ == other; }

  /** Converts this nullable pql value to a human-readable string */
  [[nodiscard]] std::string to_string() const {
    if (is_null()) {
      return std::string{cel_null_t::TEXT_REPRESENTATION};
    }

    if constexpr (std::is_same_v<T, cel_int_t> || std::is_same_v<T, cel_float_t>) {
      return std::to_string(nullable_value_.value());
    } else if constexpr (std::is_same_v<T, cel_date_t>) {
      return nullable_value_.value().to_iso_8601_timestamp();
    } else if constexpr (std::is_same_v<T, cel_boolean_t>) {
      return nullable_value_.value() ? "true" : "false";
    } else if constexpr (std::is_same_v<T, cel_string_t>) {
      return "\"" + nullable_value_.value() + "\"";
    } else if constexpr (std::is_same_v<T, cel_uuid_t>) {
      return nullable_value_.value().to_string();
    } else {
      static_assert(legacy_embedded_ctl::always_false_v<T>, "Type not implemented");
    }
  }

 private:
  std::optional<owned_cel_type_t<T>> nullable_value_{};
};

/** Null constants for different types */
constexpr nullable_pql_value<cel_int_t> NULL_INT{cel_null_v};
constexpr nullable_pql_value<cel_float_t> NULL_FLOAT{cel_null_v};
constexpr nullable_pql_value<cel_boolean_t> NULL_BOOL{cel_null_v};
inline const nullable_pql_value<cel_string_t> NULL_STR{cel_null_v};
inline const nullable_pql_value<cel_date_t> NULL_DATE{cel_null_v};
inline const nullable_pql_value<cel_uuid_t> NULL_UUID{cel_null_v};

template <typename T>
std::ostream& operator<<(std::ostream& os, const nullable_pql_value<T>& value) {
  return os << value.to_string();
}

/**
 * A vector of nullable values.
 */
template <typename T>
using nullable_vec_t = std::vector<nullable_pql_value<T>>;

template <typename T>
std::ostream& operator<<(std::ostream& os, const nullable_vec_t<T>& vec) {
  fmt::print(os, "{{{}}}", fmt::join(vec, ", "));
  return os;
}

/// Comparisons between memory::column_t and nullable vecs
template <typename T>
[[nodiscard]] bool operator==(const memory::column_t& lhs, const nullable_vec_t<T>& rhs) {
  if (lhs == nullptr || lhs->get_data_type() != get_matching_data_type<T>()) {
    return false;
  }
  return std::visit(
      [&rhs](const auto& iterable) { return std::equal(rhs.begin(), rhs.end(), iterable.begin(), iterable.end()); },
      memory::to_iterable<T>(lhs));
}

/**
 * @brief Convert from a std::vector to a nullable_vec_t. This necessarily means that there are no null values
 * in the resulting vector.
 *
 * For strings, an std::vector<std::string> is required.
 */
template <typename T>
[[nodiscard]] auto to_nullable_vec(const std::vector<T>& input) {
  static_assert(std::is_same_v<T, cel_int_t> || std::is_same_v<T, cel_float_t> || std::is_same_v<T, cel_boolean_t> ||
                    std::is_same_v<T, cel_date_t> || std::is_same_v<T, std::string> || std::is_same_v<T, cel_uuid_t>,
                "Not supported type");
  using vec_type_t = std::conditional_t<std::is_same_v<T, std::string>, cel_string_t, T>;
  nullable_vec_t<vec_type_t> output;
  output.reserve(input.size());
  std::transform(input.begin(), input.end(), std::back_inserter(output),
                 [](const auto& value) { return nullable_pql_value<vec_type_t>{value}; });
  return output;
}

using nullable_vec_variant =
    std::variant<nullable_vec_t<cel_int_t>, nullable_vec_t<cel_float_t>, nullable_vec_t<cel_string_t>,
                 nullable_vec_t<cel_date_t>, nullable_vec_t<cel_boolean_t>, nullable_vec_t<cel_uuid_t>>;

std::ostream& operator<<(std::ostream& os, const nullable_vec_variant& vec);

template <typename T>
[[nodiscard]] bool operator==(const nullable_vec_variant& lhs, const nullable_vec_t<T>& rhs) {
  const auto* const lhs_as_ptr_to_T{std::get_if<nullable_vec_t<T>>(&lhs)};
  return lhs_as_ptr_to_T != nullptr && *lhs_as_ptr_to_T == rhs;
}

/**
 * Converts the given memory::column_t to a vector of nullable values.
 */
[[nodiscard]] nullable_vec_variant to_nullable_vec(const memory::column_t& column);

template <typename T>
[[nodiscard]] nullable_vec_t<T> to_nullable_vec_typed(const memory::column_t& column) {
  return std::get<nullable_vec_t<T>>(to_nullable_vec(column));
}

}  // namespace celonis::accelerator::utils

// Catch2 requires the << overload be put into the same namespace as the type, so use memory namespace here.
namespace celonis::accelerator::memory {
std::ostream& operator<<(std::ostream& os, const celonis::accelerator::memory::column_t& column);
}
