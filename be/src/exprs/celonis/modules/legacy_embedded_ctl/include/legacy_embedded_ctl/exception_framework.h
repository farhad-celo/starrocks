#pragma once

#include <exception>
#include <string>
#include <string_view>

#include "legacy_embedded_format/json/json.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Strong type representing an error ID.
 * Error IDs represent an integer limited by a set maximal number of hex digits. The error ID is intended to be injected
 * in its hex-string representation to error messages. This allows us to match the exact error (log) e.g. from Datatog
 * with the error as reported by customers.
 * @note This is only really required for internal errors.
 */
class error_id final {
 public:
  static constexpr std::string_view ERROR_ID_KEY{"error_id"};
  [[nodiscard]] static error_id generate();
  [[nodiscard]] int as_int() const noexcept { return id_; }
  [[nodiscard]] std::string as_hex_string() const;
  explicit operator int() const noexcept { return as_int(); }

 private:
  static constexpr int NUMBER_OF_HEX_DIGITS_TO_USE{4};  // Increase in case of too many collisions
  explicit error_id(const int id) noexcept : id_{id} {}
  int id_;
};

/**
 * @brief Base class from which all of our custom exceptions should inherit
 *
 * This base class supports to store additional context in the form of JSON key-value pairs. After the construction of a
 * 'base_exception' instance, the JSON key-value storage already contains some default context data:
 * - A backtrace (key='backtrace'),
 * - The exception type as text (key='type'), and
 * - The exception message which is also returned by calling 'what()' (key='message')
 * Additional key-value pair context data can be added via 'add_or_overwrite'. As the name suggests, this adds a new
 * key-value pair or, if the key already exists, overwrites the existing value. The entire exception context can be
 * retrieved (e.g., for logging) as a single JSON string by calling 'format_as_json_string()'.
 *
 * If a derived custom exception needs to distinguish between external and internal error message communication, it can
 * override 'internal_message()' (defaults to 'what()') and must override 'external_message()'.
 *
 * The exception hierarchy can make use of multiple virtual inheritance in order for support patterns like the following
 *
 *                             std::exception
 *                            /              \
 *              base_exception                \
 *             /              \                \
 * [Custom Exceptions] [Exception Traits] [STL Exceptions]
 *             \              |                X
 *              [Most Derived Custom Exception]
 *
 * That is, all our custom exceptions should ALWAYS be catchable by both 'std::exception' and 'base_exception'.
 * Some exceptions might want to make use of "traits" (see exception_traits.h) such as annotating the exception to be
 * "repeatable". All exception traits should also always inherit from 'base_exception'.
 * @note The custom exceptions can not additionally inherit from any of STL exceptions (such as 'std::bad_alloc') as
 * indicated by the "X" in the above visualization. This is due to ambiguity as the STL exceptions do not inherit
 * virtually from 'std::exception'. Trying to do so anyways will yield a compilation error.
 */
class base_exception : public std::exception {
 protected:
  base_exception() noexcept = default;  // Required for multiple inheritance
  base_exception(std::string message, std::string_view exception_type_as_text);
  base_exception(const base_exception&) = default;
  base_exception& operator=(const base_exception&) = default;
  base_exception(base_exception&&) = default;
  base_exception& operator=(base_exception&&) = default;
  virtual ~base_exception() noexcept = default;  // NOLINT(modernize-use-override)

 public:
  /** The exception context is written into this key of the json_key_value_container_ **/
  inline static const legacy_embedded_format::json::json_key_t EXCEPTION_CONTEXT_KEY{
      "saola_exception"};  // NOLINT(cert-err58-cpp)

  [[deprecated("Use the more explicit 'internal_message()' or 'external_message()' calls.")]] [[nodiscard]] const char*
  what() const noexcept final;  // final as internal-/external_message should be overloaded
  /** Message to be used for internal communication (defaults to 'what()') */
  [[nodiscard]] virtual std::string internal_message() const;
  /** Message to be used for external communication (defaults to 'what()') */
  [[nodiscard]] virtual std::string external_message() const;
  /** Returns a JSON object with key #EXCEPTION_CONTEXT_KEY that contains additional data stored for the exception. */
  [[nodiscard]] const legacy_embedded_format::json::json_object_t& json_key_value_container() const noexcept;
  /** Allows to add further key-value pairs to the internal JSON map */
  auto add_or_overwrite(const legacy_embedded_format::json::json_key_t& key,
                        const legacy_embedded_format::json::json_value_t& value) {
    return json_exception_context().insert_or_assign(key, value);
  }
  /** Same as above but supports batch input */
  void add_or_overwrite(const legacy_embedded_format::json::json_object_t& json_key_value_container);

 protected:
  static constexpr std::string_view BACKTRACE_KEY{"backtrace"};
  // To make virtual multiple inheritance easier to work with, this can be called from the ctor bodies of sub classes
  void init(std::string message, std::string_view exception_type_as_text);
  /** Returns the JSON object that contains additional data stored for the exception. **/
  [[nodiscard]] legacy_embedded_format::json::json_object_t& json_exception_context();
  /**Do not change the order of these members! 'gdb_data_table_cache_content_command.py' relies on this memory layout*/
  std::string error_message_{};  // NOLINT(misc-non-private-member-variables-in-classes)
  legacy_embedded_format::json::json_object_t
      json_key_value_container_{};  // NOLINT(misc-non-private-member-variables-in-classes)
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
