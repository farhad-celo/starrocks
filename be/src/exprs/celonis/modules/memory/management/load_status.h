#pragma once

#include "modules/common/int_types.h"

namespace celonis::accelerator::memory::management {

/**
 * @brief describes the load status of an object:
 * - LOADED: is fully (non-compressed) loaded in-memory
 * - COMPRESSED: is compressed in-memory
 * - SWAPPED: is swapped to disk
 */
enum class load_status : int8_t { LOADED, COMPRESSED, SWAPPED };

[[nodiscard]] constexpr const char* to_string(const load_status status) noexcept {
  switch (status) {
    case load_status::LOADED:
      return "loaded";
    case load_status::COMPRESSED:
      return "compressed";
    case load_status::SWAPPED:
      return "swapped";
  }
  return "unknown";
}

}  // namespace celonis::accelerator::memory::management
