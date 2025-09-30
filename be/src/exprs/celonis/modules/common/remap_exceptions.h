#pragma once

#include <cpml/exception.h>

#include "modules/common/exceptions.h"

namespace celonis::accelerator::common {

/**
 * Remaps cpml::invalid_argument to a cpm::exception to indicate a user error. Please note that such a remapping may not
 * always be valid - in particular, cpml::invalid_argument may instead be mapped to an common::internal_exception, if
 * the exception results due to a programmer error on the library consumer side.
 *
 * This also adds the original's exceptions context to the json_context under 'original_exception_context'.
 */
[[nodiscard]] cpm_exception remap_to_cpm_exception(const cpml::invalid_argument& exception);

}  // namespace celonis::accelerator::common
