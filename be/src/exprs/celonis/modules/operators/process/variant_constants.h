#pragma once

#include "modules/common/int_types.h"

namespace celonis::accelerator::operators::process {
// grain size for computing variants themselves
constexpr size_t COMPUTE_VARIANTS_GRAIN_SIZE{1u << 15};

// grain size for utilizing computed variants
constexpr size_t VARIANT_UTILIZATION_GRAIN_SIZE{1024};
}  // namespace celonis::accelerator::operators::process