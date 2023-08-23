#pragma once

#include <string_view>

#include "modules/common/int_types.h"

namespace celonis::accelerator::log {

/**
 * The attribute used by Datadog to display the human-readable content of a log message:
 * https://docs.datadoghq.com/logs/log_configuration/attributes_naming_convention/#reserved-attributes
 */
static constexpr const char* DATADOG_LOG_MESSAGE_ATTRIBUTE{"message"};

/**
 * @brief Enum depicting keys of metadata that improve the distinction of log and trace messages of different engine
 * processes.
 */
enum class metadata_key : int8_t {
  QUERY_ENGINE_VERSION,
  DATAMODEL_ID,
  DATAMODEL_NAME,
  TEAM_ID,
  LOAD_VERSION,
  ACCELERATOR_ID,
  QUERY_ID,
  QUERY_BATCHLIST_ID,
  LOG_TIMESTAMP,
  LOG_THREAD,
  LOG_LEVEL
};

/**
 * @brief Incoming request types processed by the engine
 */
enum class request_type {
  DATA_MODEL_INFO,
  QUERY,
  DATA_MODEL_UNLOAD,
  DATA_MODEL_LOAD,
  STATISTICS,
  MANAGE_DATA,
  CREATE_AUGMENTATION_TABLE,
  REMOVE_AUGMENTATION_TABLE,
  UNKNOWN
};

/**
 * @brief String converter to the metadata keys.
 */
[[nodiscard]] const char* to_string(metadata_key key);

/**
 * @brief String converter for a request type
 */
[[nodiscard]] std::string_view to_string_view(request_type request_type);

/**
 * @brief String converter for a request type
 */
[[nodiscard]] std::string to_span_name(request_type request_type);

}  // namespace celonis::accelerator::log