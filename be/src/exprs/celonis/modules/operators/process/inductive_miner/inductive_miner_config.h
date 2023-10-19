#pragma once

#include <optional>

#ifndef CELOSTAR
#include "modules/common/execution_context_fwd.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_fwd.h"
#endif
#include "modules/operators/process/inductive_miner/directly_follows_graph_fwd.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_config.h"
#ifndef CELOSTAR
#include "modules/operators/process/variant_constants.h"
#endif

namespace celonis::accelerator::operators::process {

#ifndef CELOSTAR
struct tmp_replay_input {
  memory::column_t activity_column;
  memory::column_t case_column;
};
#endif

class inductive_miner_config final {
 public:
  inductive_miner_config(splittable_eventlog_config_t splittable_eventlog_config,
#ifdef CELOSTAR
                         const common::execution_context& parent_context, size_t grain_size,
                         const dfg_filter_config& filter_config = {});
#else
                         const common::execution_context& parent_context, size_t grain_size);
#endif
  [[nodiscard]] const splittable_eventlog_config_t& eventlog_config() const;
  [[nodiscard]] const splittable_eventlog& eventlog() const;
  [[nodiscard]] splittable_eventlog& eventlog();
  [[nodiscard]] const common::execution_context& execution_context() const;
  [[nodiscard]] size_t grain_size() const;
  [[nodiscard]] const dfg_filter_config& filter_config() const;
  [[nodiscard]] dfg_filter_config& filter_config();

#ifndef CELOSTAR
  // All member functions below will be removed in a follow-up (CPL-9267)
  [[nodiscard]] bool is_for_eventlog_based_approach() const;
  [[nodiscard]] bool is_for_variant_based_approach() const;
#endif

 private:
  splittable_eventlog_config_t splittable_eventlog_config_;
  splittable_eventlog splittable_eventlog_;
  const common::execution_context& execution_context_;
  size_t grain_size_;
  dfg_filter_config filter_config_{};
};

#ifndef CELOSTAR
[[nodiscard]] inductive_miner_config make_inductive_miner_config_for_eventlog_based_approach(
    memory::column_t activities, memory::column_t cases, cube::filter_bitset_t selections,
    const common::execution_context& parent_context, size_t grain_size);
[[nodiscard]] inductive_miner_config make_inductive_miner_config_for_eventlog_based_approach(
    memory::column_t activities, memory::column_t cases, const common::execution_context& parent_context,
    size_t grain_size);
[[nodiscard]] inductive_miner_config make_inductive_miner_config_for_variant_based_approach(
    memory::cache::variant_trace_cache_t variant_trace_cache_ptr, const common::execution_context& parent_context,
    size_t grain_size = VARIANT_UTILIZATION_GRAIN_SIZE);
#endif

}  // namespace celonis::accelerator::operators::process
