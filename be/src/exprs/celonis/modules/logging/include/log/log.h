#pragma once

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "legacy_embedded_format/json/json_fwd.h"
#include "modules/common/int_types.h"

namespace celonis::accelerator::log {

void info(const std::string& s);

void warn(const std::string& s);

void debug(const std::string& s);

void error(const std::string& s);

/**
 * Log to the default logger on log level \a info.
 *
 * @param fmt Format string for log message
 * @param args Arguments to fill placeholders within specified log message @param fmt
 */
template <typename... ARGS, typename = std::enable_if<sizeof...(ARGS) != 0>>
void info(fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  info(fmt::format(fmt, std::forward<ARGS>(args)...));
}

/**
 * Log to the default logger on log level \a warn.
 *
 * @param fmt Format string for log message
 * @param args Arguments to fill placeholders within specified log message @param fmt
 */
template <typename... ARGS, typename = std::enable_if<sizeof...(ARGS) != 0>>
void warn(fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  warn(fmt::format(fmt, std::forward<ARGS>(args)...));
}

/**
 * Log to the default logger on log level \a debug.
 *
 * @param fmt Format string for log message
 * @param args Arguments to fill placeholders within specified log message @param fmt
 */
template <typename... ARGS, typename = std::enable_if<sizeof...(ARGS) != 0>>
void debug(fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  debug(fmt::format(fmt, std::forward<ARGS>(args)...));
}

/**
 * Log to the default logger on log level \a error.
 *
 * @param fmt Format string for log message
 * @param args Arguments to fill placeholders within specified log message @param fmt
 */
template <typename... ARGS, typename = std::enable_if<sizeof...(ARGS) != 0>>
void error(fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  error(fmt::format(fmt, std::forward<ARGS>(args)...));
}

/**
 * Log to the default logger on log level \a info structured as JSON
 *
 * @param message General description of the info
 * @param details Details of the info as a JSON object
 */
void jinfo(const std::string& message, const legacy_embedded_format::json::json_object_t& details = {});

/**
 * Log to the default logger on log level \a warning structured as JSON
 *
 * @param message General description of the warning
 * @param details Details of the warning as a JSON object
 */
void jwarn(const std::string& message, const legacy_embedded_format::json::json_object_t& details = {});

/**
 * Log to the default logger on log level \a debug structured as JSON
 *
 * @param message General description of the debug
 * @param details Details of the debug as a JSON object
 */
void jdebug(const std::string& message, const legacy_embedded_format::json::json_object_t& details = {});

/**
 * Log to the default logger on log level \a error structured as JSON
 *
 * @param message General description of the error
 * @param details Details of the error as a JSON object
 */
void jerror(const std::string& message, const legacy_embedded_format::json::json_object_t& details = {});

/**
 * Force logger to flush.
 */
void flush();

}  // namespace celonis::accelerator::log
