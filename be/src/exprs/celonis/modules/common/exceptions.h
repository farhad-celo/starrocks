#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <fmt/format.h>

#include <ctl/exception_framework.h>
#include <ctl/exception_traits.h>
#include <format/json/json.h>

namespace celonis::accelerator::common {

/**
 * @brief Simple strong type representing an external error message. Only allows for ({fmt} aware) construction and
 * getting its string representation.
 */
class external_error_message final {
 public:
  [[nodiscard]] external_error_message() noexcept = default;
  [[nodiscard]] explicit external_error_message(std::string message) noexcept : value_{std::move(message)} {}
  template <typename... ARGS>
  requires(sizeof...(ARGS) != 0)
      [[nodiscard]] explicit external_error_message(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : external_error_message{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
  [[nodiscard]] const std::string& value() const& noexcept { return value_; }

 private:
  std::string value_;
};

/** Exceptions inheriting from this class indicate a server error */
class internal_exception : public virtual ctl::base_exception, public ctl::internal_error {
 public:
  [[nodiscard]] explicit internal_exception(const std::string& message);

  /** Allows to pass an additional message which is communicated externally as error reason */
  [[nodiscard]] internal_exception(const std::string& message, const external_error_message& external_message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit internal_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : internal_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}

  /** Utility factory which creates the exception and stores the given JSON data in it */
  [[nodiscard]] static internal_exception with_context(const format::json::json_object_t& json_key_value_container,
                                                       const std::string& message);

  /** Same as above but supports to pass a format string */
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] static internal_exception with_context(const format::json::json_object_t& json_key_value_container,
                                                       fmt::format_string<ARGS...> fmt, ARGS&&... args) {
    return with_context(json_key_value_container, fmt::format(std::move(fmt), std::forward<ARGS>(args)...));
  }

  /** Utility as the 'external message' ctor does not allow to pass a {fmt} string for the internal message */
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] static internal_exception with_external_message(const external_error_message& external_message,
                                                                fmt::format_string<ARGS...> fmt, ARGS&&... args) {
    return internal_exception{fmt::format(std::move(fmt), std::forward<ARGS>(args)...), external_message};
  }

 protected:
  [[nodiscard]] internal_exception() = default;
  struct constructor_tag {};
  /** Delegating ctor for the initialization of the internal_error trait */
  [[nodiscard]] internal_exception(constructor_tag t, std::string_view external_message_prefix,
                                   std::optional<const std::string_view> optional_external_message_reason);
};

/** Exceptions inheriting from this class indicate a server error which might be resolved by retrying */
class retryable_internal_exception : public internal_exception, public ctl::retryable_error {
 public:
  [[nodiscard]] explicit retryable_internal_exception(const std::string& message);

  /** Allows to pass an additional message which is communicated externally as error reason */
  [[nodiscard]] retryable_internal_exception(const std::string& message,
                                             const external_error_message& external_message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit retryable_internal_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : retryable_internal_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}

  /** Utility factory which creates the exception and stores the given JSON data in it */
  [[nodiscard]] static retryable_internal_exception with_context(
      const format::json::json_object_t& json_key_value_container, const std::string& message);

  /** Same as above but supports to pass a format string */
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] static retryable_internal_exception with_context(
      const format::json::json_object_t& json_key_value_container, fmt::format_string<ARGS...> fmt, ARGS&&... args) {
    return with_context(json_key_value_container, fmt::format(std::move(fmt), std::forward<ARGS>(args)...));
  }

  /** Utility as the 'external message' ctor does not allow to pass a {fmt} string for the internal message */
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] static retryable_internal_exception with_external_message(
      const external_error_message& external_message, fmt::format_string<ARGS...> fmt, ARGS&&... args) {
    return retryable_internal_exception{fmt::format(std::move(fmt), std::forward<ARGS>(args)...), external_message};
  }
};

/** Exceptions inheriting from this class indicate a client error */
class cpm_exception : public ctl::base_exception {
 public:
  [[nodiscard]] explicit cpm_exception(std::string m);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit cpm_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : cpm_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}

 protected:
  struct constructor_tag {};
  [[nodiscard]] cpm_exception(constructor_tag t, std::string message, std::string_view exception_type_as_text);
};

// TODO(n.weber): Candidate for removal. Exception is used only once.
class merge_eventlog_inconsistent_exception final : public cpm_exception {
 public:
  [[nodiscard]] explicit merge_eventlog_inconsistent_exception(std::string m);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit merge_eventlog_inconsistent_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : merge_eventlog_inconsistent_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

class event_table_configuration_exception final : public cpm_exception {
 public:
  [[nodiscard]] explicit event_table_configuration_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit event_table_configuration_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : event_table_configuration_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

class export_exception final : public internal_exception {
 public:
  [[nodiscard]] explicit export_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit export_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : export_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

class file_exception final : public retryable_internal_exception {
 public:
  [[nodiscard]] explicit file_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit file_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : file_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

// TODO(n.weber): The usage of this seems to indicate mostly (only?) internal errors. Double check
class invalid_input_exception : public cpm_exception {
 public:
  [[nodiscard]] explicit invalid_input_exception(std::string m);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit invalid_input_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : invalid_input_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

// TODO(n.weber): Candidate for removal. Exception is used only once. Also, seems to be an internal error
class invalid_interval_exception final : public cpm_exception {
 public:
  [[nodiscard]] invalid_interval_exception(const std::string& interval_begin, const std::string& interval_end);
};

class join_exception final : public cpm_exception {
 public:
  [[nodiscard]] explicit join_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit join_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : join_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

class operator_implementation_not_found_exception final : public cpm_exception {
 public:
  [[nodiscard]] explicit operator_implementation_not_found_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit operator_implementation_not_found_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : operator_implementation_not_found_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

class operator_not_found_exception final : public cpm_exception {
 public:
  [[nodiscard]] explicit operator_not_found_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit operator_not_found_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : operator_not_found_exception(fmt::format(fmt, std::forward<ARGS>(args)...)) {}
};

class pu_operator_exception final : public cpm_exception {
 public:
  [[nodiscard]] explicit pu_operator_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit pu_operator_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : pu_operator_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

class out_of_bounds_exception final : public internal_exception {
 public:
  [[nodiscard]] out_of_bounds_exception(const std::string& context, int64_t lower_bound, int64_t upper_bound,
                                        int64_t index);
};

class decompression_exception final : public internal_exception {
 public:
  [[nodiscard]] explicit decompression_exception(const std::string& message);

  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit decompression_exception(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      : decompression_exception{fmt::format(fmt, std::forward<ARGS>(args)...)} {}
};

class invalid_float_value_exception final : public cpm_exception {
 public:
  [[nodiscard]] invalid_float_value_exception();
};

namespace details {

[[noreturn]] void runtime_assert_fail(std::string_view message);

[[noreturn]] void runtime_assert_fail(const format::json::json_object_t& obj, std::string_view message);

template <class... ARGS>
[[noreturn]] void runtime_assert_fail(fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  runtime_assert_fail(fmt::format<ARGS...>(fmt, std::forward<ARGS>(args)...));
}

template <class... ARGS>
[[noreturn]] void runtime_assert_fail(const format::json::json_object_t& obj, fmt::format_string<ARGS...> fmt,
                                      ARGS&&... args) {
  runtime_assert_fail(obj, fmt::format<ARGS...>(fmt, std::forward<ARGS>(args)...));
}

}  // namespace details

inline void runtime_assert(bool condition, std::string_view message) {
  if (!condition) [[unlikely]] {
    details::runtime_assert_fail(message);
  }
}

inline void runtime_assert(bool condition, const format::json::json_object_t& obj, std::string_view message) {
  if (!condition) [[unlikely]] {
    details::runtime_assert_fail(obj, message);
  }
}

template <class... ARGS>
inline void runtime_assert(bool condition, fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  if (!condition) [[unlikely]] {
    details::runtime_assert_fail(fmt, std::forward<ARGS>(args)...);
  }
}

template <class... ARGS>
inline void runtime_assert(bool condition, const format::json::json_object_t& obj, fmt::format_string<ARGS...> fmt,
                           ARGS&&... args) {
  if (!condition) [[unlikely]] {
    details::runtime_assert_fail(obj, fmt, std::forward<ARGS>(args)...);
  }
}

}  // namespace celonis::accelerator::common
