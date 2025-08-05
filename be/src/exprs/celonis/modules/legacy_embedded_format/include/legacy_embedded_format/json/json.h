#pragma once

#include "json_fwd.h"  // IWYU pragma: export

namespace celonis::accelerator::legacy_embedded_format::json {
/**
 * @brief Represents structures in JSON format: https://www.json.org/json-en.html
 *
 * Usage:
 * - Initiate a JSON structure:
 *   legacy_embedded_format::json::json_t root;
 * - The type of a JSON element can be:
 *   Integer, floating point number, Boolean, string type, JSON object, JSON array
 * - The default type is a JSON object
 * - A JSON object is a associative key-value container. Modify elements the same way like a std::map
 * - A JSON array is a sequence container. Modify elements the same way like a std::vector
 *
 * Examples:
 * - add a number:
 *   root["key_for_number"] = 1;
 * - add a JSON object which can be modified afterwards:
 *   auto& child_object = root["key_for_object"];
 *   child_object["key_for_string"] = "string";
 * - add a JSON array with fixed elements:
 *   root["key_for_array"] = legacy_embedded_format::json::json_array_t{1, 2, 3, 4};
 * - add a JSON array which can be modified afterwards:
 *   auto& json_array = root["key_for_array"];   // creates JSON element with default type
 *   json_array = legacy_embedded_format::json::json_array_t{};  // change JSON element to be an array
 */
class json_value : public json_value_base_t {
 public:
  using value_type = json_value;
  using reference = value_type&;
  using const_reference = const value_type&;
  using size_type = std::size_t;

  // The default constructor defines the JSON structure that is constructed by default
  json_value() noexcept : json_value_base_t{default_json_value_t{}} {}

  /**
   * We need more sensible constructors because in C++17 std::variant constructors
   * are doing undesirable castings, e.g.
   *     std::variant<std::string, bool> example("abc")
   * constructs a bool variant instead of a string variant.
   *
   * This is a known limitation of std::variant and shall be fixed with C++20:
   * http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0608r3.html
   *
   * cppreference already documents the correct behavior of std::variant with C++20:
   * https://en.cppreference.com/w/cpp/utility/variant/variant
   * std::variant<std::string, bool> y("abc"); // OK, chooses string; bool is not
   * a candidate
   *
   * Note regarding NOLINT(google-explicit-constructor):
   * All constructors are intentionally not explicit because explicit constructor would require
   * more operator= overloads to allow implicit assignments.
   * See also: https://www.foonathan.net/2017/10/explicit-assignment/
   */
  json_value(std::nullptr_t) noexcept : json_value_base_t{nullptr} {}  // NOLINT(google-explicit-constructor)
  json_value(bool x) noexcept : json_value_base_t{x} {}                // NOLINT(google-explicit-constructor)

  template <typename T, typename std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>* = nullptr>
  json_value(T t) noexcept : json_value_base_t{static_cast<int64_t>(t)} {}  // NOLINT(google-explicit-constructor)

  template <typename T, typename std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>>* = nullptr>
  json_value(T t) noexcept : json_value_base_t{static_cast<uint64_t>(t)} {}  // NOLINT(google-explicit-constructor)

  template <typename T, typename std::enable_if_t<std::is_floating_point_v<T>>* = nullptr>
  json_value(T t) noexcept : json_value_base_t{static_cast<double>(t)} {}  // NOLINT(google-explicit-constructor)

  json_value(const char* s) : json_value_base_t{std::string{s}} {}       // NOLINT(google-explicit-constructor)
  json_value(const std::string& s) : json_value_base_t{s} {}             // NOLINT(google-explicit-constructor)
  json_value(std::string&& s) : json_value_base_t{std::move(s)} {}       // NOLINT(google-explicit-constructor)
  json_value(std::string_view s) : json_value_base_t{std::string{s}} {}  // NOLINT(google-explicit-constructor)

  json_value(const json_array_t& values) : json_value_base_t{values} {}  // NOLINT(google-explicit-constructor)
  json_value(json_array_t&& values) noexcept                             // NOLINT(google-explicit-constructor)
      : json_value_base_t{std::move(values)} {}

  json_value(const json_object_t& values) : json_value_base_t{values} {}  // NOLINT(google-explicit-constructor)
  json_value(json_object_t&& values) noexcept                             // NOLINT(google-explicit-constructor)
      : json_value_base_t{std::move(values)} {}

  // Prevent polymorphic use because we are deriving from a class without a virtual dtor
  static void* operator new(size_t) = delete;
  static void* operator new[](size_t) = delete;

  // Redirects for json_array_t (std::vector)
  reference operator[](size_type pos);
  const_reference operator[](size_type pos) const;
  void push_back(const value_type& value);
  void push_back(value_type&& value);

  // Redirects for json_object_t (std::map)
  reference operator[](const json_key_t& key);
  reference operator[](json_key_t&& key);

  /**
   * Serialization function for JSON. Invalid UTF-8 characters are replaced with U+FFFD.
   */
  [[nodiscard]] std::string to_string() const;
};

/**
 * Serialization function for JSON. Invalid UTF-8 characters are replaced with U+FFFD.
 */
std::string to_string(const json_t& json);

}  // namespace celonis::accelerator::legacy_embedded_format::json
