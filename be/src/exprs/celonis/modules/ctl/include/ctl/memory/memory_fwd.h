#pragma once

#include "ctl/memory/resource_owning_allocator.h"
#include "ctl/memory/tracking_memory_resource.h"

namespace celonis::accelerator::ctl {

template <typename T>
using default_tracking_allocator_t = ctl::resource_owning_allocator<T>;

}  // namespace celonis::accelerator::ctl