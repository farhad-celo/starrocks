#pragma once

#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"

namespace celonis::accelerator::legacy_embedded_ctl {

// TODO(a.swoboda) also use in memory_checked_node_allocator and maybe other places
struct memory_tracking_options {
  utils::allocation_reason reason{LEGACY_EMBEDDED_ALLOC_MSG(EMPTY_PLACEHOLDER_ALLOCATION_MSG)};
  std::size_t threshold_for_memory_check_in_bytes{65536};
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
