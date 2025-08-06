#pragma once

#include <span>
#include <vector>

#include "modules/operators/process/alignment/input_output_mapper.h"
#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

// This aligns a trace to a sequence run (so sequence_aligner is not quite symmetric). It never outputs unmapped moves.
class sequence_aligner {
 public:
  // TODO (goulart.e) move these two types to petri_net_entities.h
  // TODO (goulart.e) replace row_id by visible_label_type for Petri net stuff
  using visible_label_type = string_to_int_mapper::label_id_t;
  using sequence_type = std::vector<std::pair<petri_net::petri_net_transition_id, visible_label_type>>;

  /**
   * Computes the optimal alignment between a trace and a model run. This is similar to the LCS problem.
   *
   * @param trace The trace to be aligned
   * @param model_run The model run to align the trace
   * @param max_iterations The maximum number of nodes to expand during search
   * @return The optimal alignment if the computation terminated, or a null optional if it timed out
   */
  [[nodiscard]] static trace_alignment_t align_sequence_to_run(std::span<const row_id> trace,
                                                               const sequence_type& model_run, int max_iterations,
                                                               const common::execution_context& context);

  sequence_aligner(sequence_type optimal_run, int max_iterations) noexcept
      : baseline_{std::move(optimal_run)}, max_iterations_{max_iterations} {}

  [[nodiscard]] trace_alignment operator()(std::span<const row_id> trace,
                                           const common::execution_context& context) const;

 private:
  sequence_type baseline_{};
  int max_iterations_{};
};

}  // namespace celonis::accelerator::operators::process::alignment
