#pragma once
#include "legacy_embedded_ctl/bits/dynamic_bitset_types.h"

namespace celonis::accelerator::legacy_embedded_ctl {

using parallelism_settings_t = details::bitset_types::parallelism_setting;

template <parallelism_settings_t PARALLELISM_SETTING = details::bitset_types::parallelism_setting{false}>
class bitset_view;

template <parallelism_settings_t PARALLELISM_SETTING = details::bitset_types::parallelism_setting{false}>
class bitset_mutable_view;

using bitset_view_t = bitset_view<parallelism_settings_t{false}>;
using bitset_mutable_view_t = bitset_mutable_view<parallelism_settings_t{false}>;
using bitset_view_parallel_t = bitset_view<parallelism_settings_t{true}>;
using bitset_mutable_view_parallel_t = bitset_mutable_view<parallelism_settings_t{true}>;

}  // namespace celonis::accelerator::legacy_embedded_ctl