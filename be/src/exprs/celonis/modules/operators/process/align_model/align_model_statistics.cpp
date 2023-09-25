#include "align_model_statistics.h"

#include "modules/cube/execution/tracking/operator_tracker.h"

namespace celonis::accelerator::operators::process::align_model {
void align_model_statistics::log_to_operator_statistics(
    const cube::execution::tracking::add_telemetry_counter_fn& add_telemetry_counter) const {
  add_telemetry_counter("variant_count", variant_count);
  add_telemetry_counter("pruned_variants_count", pruned_variants_count);
  add_telemetry_counter("pruned_variants_computed_optimal", pruned_variants_computed_optimal);
  add_telemetry_counter("pruned_variants_computed_relaxation_labeling", pruned_variants_computed_relaxation_labeling);
  add_telemetry_counter("optimizations_solved", optimizations_solved);
  add_telemetry_counter("alignment_cost", alignment_cost);
  add_telemetry_counter("time_optimal", time_optimal);
  add_telemetry_counter("time_relaxation_labeling", time_relaxation_labeling);
  add_telemetry_counter("time_variant_alignment", time_variant_alignment);
  add_telemetry_counter("time_variant_replay", time_variant_replay);
  add_telemetry_counter("time_inflation", time_inflation);
}
}  // namespace celonis::accelerator::operators::process::align_model