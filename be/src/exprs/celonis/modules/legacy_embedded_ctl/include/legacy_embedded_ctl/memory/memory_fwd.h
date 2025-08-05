#pragma once

#include "legacy_embedded_ctl/memory/resource_owning_allocator.h"
#include "legacy_embedded_ctl/memory/tracking_memory_resource.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <typename T>
using default_tracking_allocator_t = legacy_embedded_ctl::resource_owning_allocator<T>;

}  // namespace celonis::accelerator::legacy_embedded_ctl