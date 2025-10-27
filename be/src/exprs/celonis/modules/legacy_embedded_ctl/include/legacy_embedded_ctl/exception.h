#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <boost/stacktrace.hpp>
#include <fmt/format.h>

#include "legacy_embedded_ctl/exception_framework.h"
#include "legacy_embedded_ctl/exception_traits.h"
#include "legacy_embedded_format/json/json.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/** Base class proxy to be used by all custom CTL exceptions */
class ctl_exception : virtual public base_exception, public internal_error {
 protected:
  [[nodiscard]] ctl_exception() noexcept = default;  // Required for multiple inheritance
  [[nodiscard]] ctl_exception(std::string message, const std::string_view exception_type_as_text)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{"Internal error. It is not your fault. The issue will be investigated.", std::nullopt} {
    init(std::move(message), exception_type_as_text);
    // For server errors we add the backtrace of the exception to the JSON data
    add_or_overwrite(legacy_embedded_format::json::json_key_t{BACKTRACE_KEY},
                     to_string(boost::stacktrace::stacktrace()));
  }
  /** Delegating ctor for the initialization of the internal_error trait */
  [[nodiscard]] ctl_exception(const std::string_view external_message_prefix,
                              const std::optional<const std::string_view> optional_external_message_reason)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : legacy_embedded_ctl::internal_error{external_message_prefix, optional_external_message_reason} {}
};

/** Base class proxy to be used by all retryable CTL exceptions */
class retryable_ctl_exception : public ctl_exception, public retryable_error {
 protected:
  [[nodiscard]] retryable_ctl_exception(std::string message, const std::string_view exception_type_as_text)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{EXTERNAL_MESSAGE_PREFIX, std::nullopt} {
    init(std::move(message), exception_type_as_text);
    // For server errors we add the backtrace of the exception to the JSON data
    add_or_overwrite(legacy_embedded_format::json::json_key_t{BACKTRACE_KEY},
                     to_string(boost::stacktrace::stacktrace()));
  }

  [[nodiscard]] retryable_ctl_exception(std::string message, const std::string_view exception_type_as_text,
                                        const std::string_view external_message_reason)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{EXTERNAL_MESSAGE_PREFIX, external_message_reason} {
    init(std::move(message), exception_type_as_text);
    // For server errors we add the backtrace of the exception to the JSON data
    add_or_overwrite(legacy_embedded_format::json::json_key_t{BACKTRACE_KEY},
                     to_string(boost::stacktrace::stacktrace()));
  }

 private:
  static constexpr std::string_view EXTERNAL_MESSAGE_PREFIX{
      "Temporary internal error. It is not your fault. You can try again later."};
};

/**
 * @brief Exception thrown by abort_assert (@see assert.h) when aborting is disabled.
 */
class failed_assertion final : public ctl_exception {
 public:
  [[nodiscard]] failed_assertion()
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{"Failed CTL assertion.", EXCEPTION_TYPE_AS_TEXT} {}
  [[nodiscard]] explicit failed_assertion(const std::string& msg)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{fmt::format("Failed CTL assertion: {}.", msg), EXCEPTION_TYPE_AS_TEXT} {}
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit failed_assertion(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : failed_assertion{fmt::format(fmt, std::forward<ARGS>(args)...)} {}

 private:
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::failed_assertion"};
};

/**
 * @brief Exception thrown when there is an error with an invalid interval (@see interval.h).
 */
class invalid_interval final : public ctl_exception {
 public:
  [[nodiscard]] invalid_interval() = delete;
  [[nodiscard]] explicit invalid_interval(std::string msg)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{std::move(msg), EXCEPTION_TYPE_AS_TEXT} {}
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit invalid_interval(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : invalid_interval{fmt::format(fmt, std::forward<ARGS>(args)...)} {}

 private:
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::invalid_interval"};
};

/** Simple proxy to allow for catching both 'short_of_memory' and 'bad_alloc' at once */
class oom_error : public retryable_ctl_exception {
 protected:
  [[nodiscard]] oom_error(std::string msg, const std::string_view exception_type_as_text)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : retryable_ctl_exception{std::move(msg), exception_type_as_text, EXTERNAL_ERROR_MESSAGE} {}

 private:
  /** For temporary OOM errors the user is asked to retry later */
  static constexpr std::string_view EXTERNAL_ERROR_MESSAGE{
      "Failed allocation request: System is short of memory. Contact support if the problem persists."};
};

/**
 * @brief Exception thrown when trying to allocate memory but the validation that enough memory is available fails
 */
class short_of_memory final : public oom_error {
 public:
  [[nodiscard]] explicit short_of_memory(const std::string& msg)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : oom_error{fmt::format("Failed allocation request: {}.", msg), EXCEPTION_TYPE_AS_TEXT} {}
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit short_of_memory(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : short_of_memory{fmt::format(fmt, std::forward<ARGS>(args)...)} {}

  [[nodiscard]] explicit short_of_memory(const bool estimated = false)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : oom_error{
            "Failed allocation request: System is short of memory. Refusing Allocation. Try again later or contact "
            "support if the problem persists.",
            EXCEPTION_TYPE_AS_TEXT} {
    // make it possible to discern which type it is in case it is reported by a user
    error_message_ += estimated ? " (E2)" : " (E1)";
  }

 private:
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::short_of_memory"};
};

/**
 * @brief Exception thrown when a std::bad_alloc exception is caught and handled within the CTL module
 */
class bad_alloc final : public oom_error {
 public:
  [[nodiscard]] bad_alloc(const std::size_t number_of_elements_to_allocate, const std::size_t byte_size_of_each_element,
                          const std::string& allocation_reason)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : oom_error{
            fmt::format("Failed allocation request: Bad allocation - trying to allocate {} items of size {} bytes. {}",
                        number_of_elements_to_allocate, byte_size_of_each_element, allocation_reason),
            EXCEPTION_TYPE_AS_TEXT} {}

 private:
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::bad_alloc"};
};

/**
 * @brief Exception thrown when an invalid type conversion happens
 */
class conversion_exception final : public ctl_exception {
 public:
  template <typename SOURCE_TYPE>
  [[nodiscard]] conversion_exception(SOURCE_TYPE&& source_value, std::string source_typename,
                                     std::string target_typename)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{
            fmt::format("Failed type conversion: Cannot safely convert value [{}] of type [{}] to target type [{}].",
                        std::forward<SOURCE_TYPE>(source_value), std::move(source_typename),
                        std::move(target_typename)),
            EXCEPTION_TYPE_AS_TEXT} {}

 private:
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::conversion_exception"};
};

/**
 * @brief Exception thrown when there is an out of bounds access (e.g. in static_array::at())
 */
class out_of_range final : public ctl_exception {
 public:
  [[nodiscard]] out_of_range() = delete;
  [[nodiscard]] explicit out_of_range(std::string msg)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{std::move(msg), EXCEPTION_TYPE_AS_TEXT} {}
  template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
  [[nodiscard]] explicit out_of_range(fmt::format_string<ARGS...> fmt, ARGS&&... args)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : out_of_range{fmt::format(fmt, std::forward<ARGS>(args)...)} {}

 private:
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::out_of_range"};
};

/**
 * @brief Exception thrown on null dereference
 */
class null_pointer_exception final : public ctl_exception {
 public:
  [[nodiscard]] null_pointer_exception() : null_pointer_exception{"Null pointer access"} {}

 private:
  [[nodiscard]] explicit null_pointer_exception(std::string message)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{std::move(message), EXCEPTION_TYPE_AS_TEXT} {}
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::null_pointer_exception"};
};

/**
 * @brief Exception thrown on null dereference
 */
class timeout_exception final : public ctl_exception {
 public:
  [[nodiscard]] explicit timeout_exception(std::string message)
      // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
      : ctl_exception{std::move(message), EXCEPTION_TYPE_AS_TEXT} {}

 private:
  static constexpr std::string_view EXCEPTION_TYPE_AS_TEXT{"ctl::timeout_exception"};
};
}  // namespace celonis::accelerator::legacy_embedded_ctl
