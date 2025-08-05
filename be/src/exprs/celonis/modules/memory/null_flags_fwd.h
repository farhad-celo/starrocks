#pragma once

#include "legacy_embedded_ctl/dynamic_bitset_fwd.h"

namespace celonis::accelerator::memory {
using null_flags_bitset_t = legacy_embedded_ctl::dynamic_bitset_t;
using null_flags_t = std::shared_ptr<null_flags_bitset_t>;
}  // namespace celonis::accelerator::memory
