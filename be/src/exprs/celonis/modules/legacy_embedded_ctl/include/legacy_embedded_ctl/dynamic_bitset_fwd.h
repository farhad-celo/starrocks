#pragma once
#include "legacy_embedded_ctl/bits/dynamic_bitset_types.h"

namespace celonis::accelerator::legacy_embedded_ctl {

using parallelism_settings_t = details::bitset_types::parallelism_setting;

template <parallelism_settings_t PARALLELISM_SETTING = details::bitset_types::parallelism_setting{false}>
class dynamic_bitset;

using dynamic_bitset_t = dynamic_bitset<details::bitset_types::parallelism_setting{false}>;
using dynamic_bitset_parallel_t = dynamic_bitset<details::bitset_types::parallelism_setting{true}>;

}  // namespace celonis::accelerator::legacy_embedded_ctl
