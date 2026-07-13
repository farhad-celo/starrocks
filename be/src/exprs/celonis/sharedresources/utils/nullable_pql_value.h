#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/shared_types_fwd.h"

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
                          std::is_same_v<T, cel_date_t> || std::is_same_v<T, cel_string_t> ||
                          std::is_same_v<T, cel_uuid_t>,
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
            : nullable_value_{value.has_value() ? std::make_optional(owned_cel_type_t<T>{value.value()})
                                                : std::nullopt} {}

    // NOLINTNEXTLINE(google-explicit-constructor)
    nullable_pql_value(std::string value) requires(std::is_same_v<T, cel_string_t>)
            : nullable_value_{std::move(value)} {}

    // NOLINTNEXTLINE(google-explicit-constructor)
    nullable_pql_value(std::string_view value) requires(std::is_same_v<T, cel_string_t>)
            : nullable_value_{std::string{value}} {}

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

private:
    std::optional<owned_cel_type_t<T>> nullable_value_{};
};

/**
 * A vector of nullable values.
 */
template <typename T>
using nullable_vec_t = std::vector<nullable_pql_value<T>>;

} // namespace celonis::accelerator::utils