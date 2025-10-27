#include "legacy_embedded_format/json/json.h"

#include <nlohmann/json.hpp>

#include "legacy_embedded_ctl/system_constants.h"
#include "modules/common/exceptions.h"
/**
 * nlohmann JSON conversion for custom type.
 * See also other to_json function in this file
 *
 * A standard conversion for std::variant will not be added to nlohmann.
 * This implementation is taken from the nlohmann discussion:
 * https://github.com/nlohmann/json/issues/1261#issuecomment-426200060
 *
 * This approach avoids adding the conversion to std namespace.
 * This specialization must be added to the nlohmann namespace
 * See: https://github.com/nlohmann/json#how-do-i-convert-third-party-types
 */
namespace nlohmann {
template <typename... Args>
struct adl_serializer<std::variant<Args...>> {
  static void to_json(json& j, const std::variant<Args...>& variant) {
    std::visit([&](auto&& value) { j = std::forward<decltype(value)>(value); }, variant);
  }
};
}  // namespace nlohmann

/**
 * Required because std::visit is under-specified when inheriting from std::variant
 *
 * Solution is based on: https://stackoverflow.com/a/58984580
 *
 * Proposal to properly support inheritance for std::variant
 * http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2162r0.html#inheriting-from-variant
 */
namespace std {
template <>
struct variant_size<celonis::accelerator::legacy_embedded_format::json::json_value>
    : variant_size<celonis::accelerator::legacy_embedded_format::json::json_value::variant> {};

template <std::size_t I>
struct variant_alternative<I, celonis::accelerator::legacy_embedded_format::json::json_value>
    : variant_alternative<I, celonis::accelerator::legacy_embedded_format::json::json_value::variant> {};
}  // namespace std

#if defined(CEL_STD_LIB_GNU) && _GLIBCXX_RELEASE >= 9
/**
 * Visiting inherited variants doesn't work with libstdc++ >= 9. It works with libc++.
 *
 * Example not compiling with libstdc++ and clang-9: https://godbolt.org/z/o9Gvobnan
 * Example compiling with libc++ and clang-9: https://godbolt.org/z/TGKGvo34K
 *
 * A fix for libstdc++ was proposed but is not yet implemented:
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90943
 *
 * The current solution is based on the proposed fix from:
 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90943#c1
 *
 * We specialize std::__detail::__variant::_Extra_visit_slot_needed for the inherited variant type
 * and making it not never-valueless. This will generate extra code to handle the valueless case even
 * if the variant base class will never be valueless.
 */
namespace std::__detail::__variant {
template <typename _Maybe_variant_cookie>  // NOLINT(bugprone-reserved-identifier)
struct _Extra_visit_slot_needed<_Maybe_variant_cookie,
                                const celonis::accelerator::legacy_embedded_format::json::json_value&> {
  struct _Variant_never_valueless : false_type {};  // NOLINT(bugprone-reserved-identifier)

  static constexpr bool value =
      (is_same_v<_Maybe_variant_cookie, __variant_cookie> || is_same_v<_Maybe_variant_cookie, __variant_idx_cookie>) &&
      !_Variant_never_valueless::value;
};

template <typename _Maybe_variant_cookie>  // NOLINT(bugprone-reserved-identifier)
struct _Extra_visit_slot_needed<_Maybe_variant_cookie,
                                const celonis::accelerator::legacy_embedded_format::json::json_value> {
  struct _Variant_never_valueless : false_type {};  // NOLINT(bugprone-reserved-identifier)

  static constexpr bool value =
      (is_same_v<_Maybe_variant_cookie, __variant_cookie> || is_same_v<_Maybe_variant_cookie, __variant_idx_cookie>) &&
      !_Variant_never_valueless::value;
};
}  // namespace std::__detail::__variant
#endif

namespace celonis::accelerator::legacy_embedded_format::json {
/**
 * nlohmann JSON conversion for custom type.
 * This function must be in the same namespace as the custom type.
 * See: https://github.com/nlohmann/json#arbitrary-types-conversions
 */
void to_json(nlohmann::json& j, const json_value& v) {
  std::visit([&](auto&& value) { j = std::forward<decltype(value)>(value); }, v);
}

json_value::reference json_value::operator[](size_type pos) {
  if (auto* array = std::get_if<json_array_t>(this)) {
    return (*array)[pos];
  }

  throw common::internal_exception{"operator[index] only supported for JSON arrays."};
}

json_value::const_reference json_value::operator[](size_type pos) const {
  if (const auto* array = std::get_if<json_array_t>(this)) {
    return (*array)[pos];
  }

  throw common::internal_exception{"operator[index] only supported for JSON arrays."};
}

void json_value::push_back(const value_type& value) {
  if (auto* array = std::get_if<json_array_t>(this)) {
    return (*array).push_back(value);
  }

  throw common::internal_exception{"push_back only supported for JSON arrays."};
}

void json_value::push_back(value_type&& value) {
  if (auto* array = std::get_if<json_array_t>(this)) {
    return (*array).push_back(value);
  }

  throw common::internal_exception{"push_back only supported for JSON arrays."};
}

json_value::reference json_value::operator[](const json_key_t& key) {
  if (auto* map = std::get_if<json_object_t>(this)) {
    return (*map)[key];
  }

  throw common::internal_exception{"operator[key] only supported for JSON maps."};
}

json_value::reference json_value::operator[](json_key_t&& key) {
  if (auto* map = std::get_if<json_object_t>(this)) {
    return (*map)[key];
  }

  throw common::internal_exception{"operator[key] only supported for JSON maps."};
}

std::string json_value::to_string() const {
  // Don't use braced list initializer because it constructs a JSON array instead of a JSON object
  // See: https://github.com/nlohmann/json/issues/1359
  nlohmann::json j(*this);
  /*
   * Change behavior for invalid UTF-8 characters:
   * default: error_handler_t::strict -> throw exception in case of a decoding error
   * now: error_handler_t::replace -> replace invalid UTF-8 sequences with U+FFFD
   *
   * The other settings are set to their default value.
   */
  return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string to_string(const json_t& json) { return json.to_string(); }

}  // namespace celonis::accelerator::legacy_embedded_format::json
