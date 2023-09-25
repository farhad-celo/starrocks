#pragma once

#include "modules/common/int_types.h"

namespace celonis::accelerator::logging::builder {
/**
 * @brief Available log levels.
 *
 * Ordered in increasing level of severity.
 */
enum class level : int8_t {
  DEBUG, /**< Debug log level. */
  INFO,  /**< Info log level. */
  WARN,  /**< Warning log level. */
  ERROR  /**< Error log level. */
};

/**
 * @brief Default log level used in production environment.
 */
static constexpr level DEFAULT_LOG_LEVEL{level::INFO};
}  // namespace celonis::accelerator::logging::builder