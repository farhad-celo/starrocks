#pragma once

#include "legacy_embedded_ctl/bits/static_array_base_fwd.h"
#include "legacy_embedded_ctl/memory/memory_fwd.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <typename T>
using static_array_allocator_type = default_tracking_allocator_t<std::remove_const_t<T>>;

template <typename T>
using static_array = details::static_array_base<T, details::non_shared, static_array_allocator_type<T>>;

template <typename T>
using shared_static_array = details::static_array_base<T, details::shared, static_array_allocator_type<T>>;

}  // namespace celonis::accelerator::legacy_embedded_ctl
