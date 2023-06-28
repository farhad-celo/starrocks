#include "log/log.h"

#include "ctl/assert.h"
#include "format/json/json.h"
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
format::json::json_t merge_message_and_details(const std::string& message, const format::json::json_object_t& details) {
  format::json::json_t json{details};
  json[DATADOG_LOG_MESSAGE_ATTRIBUTE] = message;
  return json;
}
}  // namespace

#ifdef CELOSTAR
void info(const std::string& s) { LOG(INFO) << s; }

void warn(const std::string& s) { LOG(WARNING) << s; }

void debug(const std::string& s) { VLOG(1) << s; }

void error(const std::string& s) { LOG(ERROR) << s; }

void flush() { google::FlushLogFiles(google::INFO); }
#else
void info(const std::string& s) { logging::factories::logger()->info(s); }

void warn(const std::string& s) { logging::factories::logger()->warn(s); }

void debug(const std::string& s) { logging::factories::logger()->debug(s); }

void error(const std::string& s) { logging::factories::logger()->error(s); }

void flush() { logging::factories::logger()->flush(); }
#endif

void jinfo(const std::string& message, const format::json::json_object_t& details) {
  info(merge_message_and_details(message, details).to_string());
}

void jwarn(const std::string& message, const format::json::json_object_t& details) {
  warn(merge_message_and_details(message, details).to_string());
}

void jdebug(const std::string& message, const format::json::json_object_t& details) {
  debug(merge_message_and_details(message, details).to_string());
}

void jerror(const std::string& message, const format::json::json_object_t& details) {
  error(merge_message_and_details(message, details).to_string());
}

}  // namespace celonis::accelerator::log
