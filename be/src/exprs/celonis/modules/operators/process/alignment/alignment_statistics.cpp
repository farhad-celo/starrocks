#include "alignment_statistics.h"

#ifndef CELOSTAR
#include "modules/cube/execution/tracking/operator_statistics.h"

namespace celonis::accelerator::operators::process::alignment {

void alignment_statistics::log_to_operator_statistics(
    const cube::execution::tracking::add_telemetry_counter_fn& add_telemetry_counter) const {
  add_telemetry_counter("petri_net_visible_label_count", petri_net_visible_label_count);
  add_telemetry_counter("distinct_petri_net_visible_label_count", distinct_petri_net_visible_label_count);
  add_telemetry_counter("variant_count", variant_count);
  add_telemetry_counter("pruned_variants_count", pruned_variants_count);
  add_telemetry_counter("pruned_variants_computed_optimal", pruned_variants_computed_optimal);
  add_telemetry_counter("pruned_variants_computed_relaxation_labeling", pruned_variants_computed_relaxation_labeling);
  add_telemetry_counter("optimizations_solved", optimizations_solved);
  add_telemetry_counter("total_cost_pruned_variants", total_cost_pruned_variants);
  add_telemetry_counter("time_unfolding", time_unfolding);
  add_telemetry_counter("time_preprocess", time_preprocess);
  add_telemetry_counter("time_optimal", time_optimal);
  add_telemetry_counter("time_relaxation_labeling", time_relaxation_labeling);

  for (size_t i{}; i != size(constraints); ++i) {
    add_telemetry_counter(fmt::format("right_order_compatibility[{}]", i),
                          static_cast<size_t>(std::abs(constraints[i].right_order_compatibility)));
    add_telemetry_counter(fmt::format("wrong_order_compatibility[{}]", i),
                          static_cast<size_t>(std::abs(constraints[i].wrong_order_compatibility)));
    add_telemetry_counter(fmt::format("exclusive_compatibility[{}]", i),
                          static_cast<size_t>(std::abs(constraints[i].exclusive_compatibility)));
    add_telemetry_counter(fmt::format("parallel_compatibility[{}]", i),
                          static_cast<size_t>(std::abs(constraints[i].parallel_compatibility)));
    add_telemetry_counter(fmt::format("deletion_compatibility[{}]", i),
                          static_cast<size_t>(std::abs(constraints[i].deletion_compatibility)));
  }
}

}  // namespace celonis::accelerator::operators::process::alignment
#endif
