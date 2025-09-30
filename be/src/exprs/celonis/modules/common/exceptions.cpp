#include "exceptions.h"

#include <boost/stacktrace.hpp>

namespace celonis::accelerator::common {

namespace {

constexpr std::string_view INTERNAL_ERROR_EXTERNAL_MESSAGE_PREFIX{
    "Internal error. It is not your fault. We apologize and kindly ask you to inform Celonis support, so that it can "
    "be investigated accordingly."};
constexpr std::string_view RETRYABLE_INTERNAL_ERROR_EXTERNAL_MESSAGE_PREFIX{
    "Internal error. It is not your fault. We apologize and kindly ask you to try again later or contact support if "
    "the problem persists."};

}  // namespace

internal_exception::internal_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : internal_exception{constructor_tag{}, INTERNAL_ERROR_EXTERNAL_MESSAGE_PREFIX, std::nullopt} {
  init(message, "internal_exception");
  // For server errors we add the backtrace of the exception to the JSON data
  add_or_overwrite(format::json::json_key_t{BACKTRACE_KEY}, to_string(boost::stacktrace::stacktrace()));
}

internal_exception::internal_exception(const std::string& message, const external_error_message& external_message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : internal_exception{constructor_tag{}, INTERNAL_ERROR_EXTERNAL_MESSAGE_PREFIX, external_message.value()} {
  init(message, "internal_exception");
  // For server errors we add the backtrace of the exception to the JSON data
  add_or_overwrite(format::json::json_key_t{BACKTRACE_KEY}, to_string(boost::stacktrace::stacktrace()));
}

internal_exception internal_exception::with_context(const format::json::json_object_t& json_key_value_container,
                                                    const std::string& message) {
  internal_exception ex{message};
  ex.add_or_overwrite(json_key_value_container);
  return ex;
}

internal_exception::internal_exception([[maybe_unused]] constructor_tag t,
                                       const std::string_view external_message_prefix,
                                       const std::optional<const std::string_view> optional_external_message_reason)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : ctl::internal_error{external_message_prefix, optional_external_message_reason} {}

retryable_internal_exception::retryable_internal_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : internal_exception{constructor_tag{}, RETRYABLE_INTERNAL_ERROR_EXTERNAL_MESSAGE_PREFIX, std::nullopt} {
  init(message, "retryable_internal_exception");
  // For server errors we add the backtrace of the exception to the JSON data
  add_or_overwrite(format::json::json_key_t{BACKTRACE_KEY}, to_string(boost::stacktrace::stacktrace()));
}

retryable_internal_exception::retryable_internal_exception(const std::string& message,
                                                           const external_error_message& external_message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : internal_exception{constructor_tag{}, RETRYABLE_INTERNAL_ERROR_EXTERNAL_MESSAGE_PREFIX,
                         external_message.value()} {
  init(message, "retryable_internal_exception");
  // For server errors we add the backtrace of the exception to the JSON data
  add_or_overwrite(format::json::json_key_t{BACKTRACE_KEY}, to_string(boost::stacktrace::stacktrace()));
}

retryable_internal_exception retryable_internal_exception::with_context(
    const format::json::json_object_t& json_key_value_container, const std::string& message) {
  retryable_internal_exception ex{message};
  ex.add_or_overwrite(json_key_value_container);
  return ex;
}

cpm_exception::cpm_exception(std::string m) : cpm_exception{constructor_tag{}, std::move(m), "cpm_exception"} {}

cpm_exception::cpm_exception([[maybe_unused]] constructor_tag t, std::string message,
                             std::string_view exception_type_as_text)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : ctl::base_exception{std::move(message), exception_type_as_text} {}

merge_eventlog_inconsistent_exception::merge_eventlog_inconsistent_exception(std::string m)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{}, std::move(m), "merge_eventlog_inconsistent_exception"} {}

event_table_configuration_exception::event_table_configuration_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{}, fmt::format("Error in event log configuration. {}", message),
                    "event_table_configuration_exception"} {}

export_exception::export_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : internal_exception{fmt::format("Error while dumping the result table. {}", message)} {}

file_exception::file_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : retryable_internal_exception{message} {}

invalid_input_exception::invalid_input_exception(std::string m)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{}, std::move(m), "invalid_input_exception"} {}

invalid_interval_exception::invalid_interval_exception(const std::string& interval_begin,
                                                       const std::string& interval_end)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{},
                    fmt::format("Invalid date interval [{} , {}]. The begin of any interval must not be larger than "
                                "its end. Therefore, the interval is ignored.",
                                interval_begin, interval_end),
                    "invalid_interval_exception"} {}

join_exception::join_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{}, fmt::format("No common table could be found. {}", message), "join_exception"} {}

operator_implementation_not_found_exception::operator_implementation_not_found_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{},
                    fmt::format("Operator implementation could not be found or wrong types. {}", message),
                    "operator_implementation_not_found_exception"} {}

operator_not_found_exception::operator_not_found_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{}, fmt::format("Operator could not be found. {}", message),
                    "operator_not_found_exception"} {}

pu_operator_exception::pu_operator_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{}, fmt::format("Pull-Up-function could not be executed. {}", message),
                    "pu_operator_exception"} {}

out_of_bounds_exception::out_of_bounds_exception(const std::string& context, const int64_t lower_bound,
                                                 const int64_t upper_bound, const int64_t index)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : internal_exception{fmt::format("Out of bounds access in {}: The index has to be element of [{}, {}] but was {}.",
                                     context, lower_bound, upper_bound, index)} {}

decompression_exception::decompression_exception(const std::string& message)
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : internal_exception{message} {}

invalid_float_value_exception::invalid_float_value_exception()
    // NOLINTNEXTLINE(bugprone-throw-keyword-missing)
    : cpm_exception{constructor_tag{}, "Floating point number is a not-a-number (NaN) value.",
                    "invalid_float_value_exception"} {}

namespace details {

void runtime_assert_fail(const std::string_view message) { throw internal_exception{std::string{message}}; };

void runtime_assert_fail(const format::json::json_object_t& obj, const std::string_view message) {
  throw internal_exception::with_context(obj, std::string{message});
}

}  // namespace details
}  // namespace celonis::accelerator::common
