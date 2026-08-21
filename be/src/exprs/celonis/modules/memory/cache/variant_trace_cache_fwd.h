#pragma once

#include <memory>

#include <ctl/checked_ptr.h>

namespace celonis::accelerator::memory::cache {
class variant_trace_cache;
using variant_trace_cache_t = std::shared_ptr<variant_trace_cache>;
// Adjusted naming to indicate that the values are not necessarily cached
using variant_entries_t = ctl::checked_shared_ptr<variant_trace_cache>;
}  // namespace celonis::accelerator::memory::cache
