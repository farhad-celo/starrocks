#pragma once
#include "modules/common/int_types.h"
#include "modules/cube/execution/tracking/operator_tracker_fwd.h"

namespace celonis::accelerator::operators::process::align_model {

struct align_model_statistics {
  size_t variant_count{};
  size_t pruned_variants_count{};
  size_t pruned_variants_computed_optimal{};
  size_t pruned_variants_computed_relaxation_labeling{};
  size_t optimizations_solved{};
  size_t alignment_cost{};
  size_t time_optimal{};
  size_t time_relaxation_labeling{};
  size_t time_variant_alignment{};
  size_t time_variant_replay{};
  size_t time_inflation{};

  void log_to_operator_statistics(
      const cube::execution::tracking::add_telemetry_counter_fn& add_telemetry_counter) const;
};
}  // namespace celonis::accelerator::operators::process::align_model