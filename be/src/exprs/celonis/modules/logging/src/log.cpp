#include "log/log.h"

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_format/json/json.h"
#ifdef CELOSTAR
#include "glog/logging.h"
#endif
#include "log/metadata.h"
#ifndef CELOSTAR
#include "logging/builder/logger/spdlog_logger.h"
#include "logging/factories/logger_factory.h"
#endif

namespace celonis::accelerator::log {

namespace {
legacy_embedded_format::json::json_object_t merge_message_and_details(const std::string& message,
                                                      const legacy_embedded_format::json::json_object_t& details = {}) {
  legacy_embedded_format::json::json_object_t json{details};
  json[DATADOG_LOG_MESSAGE_ATTRIBUTE] = message;
  return json;
}

/**
 * Remove enclosing curly braces from a JSON formatted input.
 * This internal function is only called with JSON formatted input which guarantees a minimum input size of 2.
 */
std::string remove_enclosing_braces(const std::string& json_formatted_input) {
  legacy_embedded_debug_assert(std::size(json_formatted_input) >= 2);
  return json_formatted_input.substr(1, std::size(json_formatted_input) - 2);
}

std::string to_json_string_without_enclosing_braces(const legacy_embedded_format::json::json_object_t& json_input) {
  return remove_enclosing_braces(legacy_embedded_format::json::to_string(json_input));
}
}  // namespace

void info(const std::string& s) { jinfo(s); }

void warn(const std::string& s) { jwarn(s); }

void debug(const std::string& s) { jdebug(s); }

void error(const std::string& s) { jerror(s); }

#ifdef CELOSTAR
void jinfo(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
    LOG(INFO) << to_json_string_without_enclosing_braces(merge_message_and_details(message, details));
}

void jwarn(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
    LOG(WARNING) << to_json_string_without_enclosing_braces(merge_message_and_details(message, details));
}

void jdebug(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
    VLOG(1) << to_json_string_without_enclosing_braces(merge_message_and_details(message, details));
}

void jerror(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
    LOG(ERROR) << to_json_string_without_enclosing_braces(merge_message_and_details(message, details));
}

void flush() { google::FlushLogFiles(google::INFO); }
#else
void jinfo(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
  logging::factories::logger()->info(
      to_json_string_without_enclosing_braces(merge_message_and_details(message, details)));
}

void jwarn(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
  logging::factories::logger()->warn(
      to_json_string_without_enclosing_braces(merge_message_and_details(message, details)));
}

void jdebug(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
  logging::factories::logger()->debug(
      to_json_string_without_enclosing_braces(merge_message_and_details(message, details)));
}

void jerror(const std::string& message, const legacy_embedded_format::json::json_object_t& details) {
  logging::factories::logger()->error(
      to_json_string_without_enclosing_braces(merge_message_and_details(message, details)));
}

void flush() { logging::factories::logger()->flush(); }
#endif

}  // namespace celonis::accelerator::log
