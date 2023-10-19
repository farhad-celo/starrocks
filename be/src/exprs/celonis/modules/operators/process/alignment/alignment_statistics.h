#pragma once

#include "modules/common/int_types.h"
#ifndef CELOSTAR
#include "modules/cube/execution/tracking/operator_tracker_fwd.h"
#endif
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"

namespace celonis::accelerator::operators::process::alignment {

struct alignment_statistics {
  size_t petri_net_visible_label_count{};
  size_t distinct_petri_net_visible_label_count{};
  size_t variant_count{};
  size_t pruned_variants_count{};
  size_t pruned_variants_computed_optimal{};
  size_t pruned_variants_computed_relaxation_labeling{};
  size_t optimizations_solved{};             // Number of RL optimizations solved = #_RL_VARIANTS * AVG_#_CONFIGS
  size_t successful_relaxation_labelings{};  // Relaxation labeling might fail because of completion step
  size_t total_cost_pruned_variants{};
  size_t successfully_computed_pruned_variants{};
  size_t time_unfolding{};
  size_t time_preprocess{};
  size_t time_optimal{};  // Might be too short to measure accurately
  size_t time_relaxation_labeling{};

  std::vector<rl_align::constraints_config> constraints{};

#ifndef CELOSTAR
  void log_to_operator_statistics(
      const cube::execution::tracking::add_telemetry_counter_fn& add_telemetry_counter) const;
#endif
};

}  // namespace celonis::accelerator::operators::process::alignment
