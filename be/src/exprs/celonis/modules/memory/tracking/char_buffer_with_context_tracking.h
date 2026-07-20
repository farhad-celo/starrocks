#pragma once

#include "modules/common/buffer_types.h"
#include "spawn_allocator_from_context.h"

namespace celonis::accelerator::memory::tracking {

/**
 * @brief Factory that tries to allocate and return a dynamic bitset for 'size' using an allocator spawned from context.
 */
[[nodiscard]] common::char_buffer make_tracked_char_buffer(const common::execution_context& context);

/*
 ***********************************************************************************************************************
 *** Implementation section of above declarations
 ***********************************************************************************************************************
 */

inline common::char_buffer make_tracked_char_buffer(const common::execution_context& context) {
  return common::char_buffer{
      spawn_allocator<char>(context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))};
}

}  // namespace celonis::accelerator::memory::tracking
