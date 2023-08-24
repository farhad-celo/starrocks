#pragma once

#include <variant>

#ifdef CELOSTAR
#include "exprs/celonis/variant.h"
#endif
#include "modules/common/execution_context_fwd.h"
#include "modules/common/int_types.h"
#ifndef CELOSTAR
#include "modules/cube/filter_bitset.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_fwd.h"
#endif

namespace celonis::accelerator::operators::process {

#ifdef CELOSTAR
/** Config with all the necessary data to extract the variant map */
struct splittable_eventlog_config_for_using_variant_map {
  const starrocks::VariantHashMap& variant_map;
  const size_t grain_size;
};

using splittable_eventlog_config_t = std::variant<splittable_eventlog_config_for_using_variant_map>;
#else
/** Config with all the necessary data to extract the entire eventlog */
struct splittable_eventlog_config_for_using_entire_eventlog final {
  const memory::column_t activities;
  const memory::column_t cases;
  const size_t grain_size;
  /**
   * Again, `event_log_selections` must be as large as `activity_column`. Its
   * i-th entry is `true` iff the i-th event in `activity_column` is to be
   * considered by the miner, i.e. the i-th event shall not be skipped.
   */
  const cube::filter_bitset_t selections;
};

/** Config with all the necessary data to extract the variants */
struct splittable_eventlog_config_for_using_variants final {
  const memory::cache::variant_trace_cache_t variant_trace_cache_ptr;
  const size_t grain_size;
};

using splittable_eventlog_config_t =
    std::variant<splittable_eventlog_config_for_using_entire_eventlog, splittable_eventlog_config_for_using_variants>;
#endif

/** Retrieves the grain_size from the config */
[[nodiscard]] size_t grain_size_from_splittable_eventlog_config(const splittable_eventlog_config_t& config);

#ifdef CELOSTAR
/** Factory for the Celostar 'variant_map' case */
[[nodiscard]] splittable_eventlog_config_t make_splittable_eventlog_config(
    const starrocks::VariantHashMap& variant_map, const size_t grain_size);
#else
/** Factory for the 'entire eventlog' case */
[[nodiscard]] splittable_eventlog_config_t make_splittable_eventlog_config(
    memory::column_t activities, memory::column_t cases, size_t grain_size, cube::filter_bitset_t selections,
    const common::execution_context& operator_context);

[[nodiscard]] splittable_eventlog_config_t make_splittable_eventlog_config(
    memory::column_t activities, memory::column_t cases, size_t grain_size,
    const common::execution_context& operator_context);

/** Factory for the 'variant' case */
[[nodiscard]] splittable_eventlog_config_t make_splittable_eventlog_config(
    memory::cache::variant_trace_cache_t variant_trace_cache_ptr, size_t grain_size);
#endif

}  // namespace celonis::accelerator::operators::process
