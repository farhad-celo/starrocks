#pragma once

#include <optional>
#include <unordered_set>
#include <vector>

#include "modules/operators/process/petri_net/petri_net_fwd.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment::fitting_prefix {

/**
 * Tries to align the trace such that all trace variables have a synchronous move
 *  and visible model moves only appear at the end of the alignment.
 * @param pn_accessor the petri net
 * @param variant the trace variant
 * @param max_trailing_model_moves maximum number of visible trailing model moves
 * @param iterations maximum number of nodes to expand
 * @return An optional containing the corresponding trace alignment. This is empty if no such alignment exists
 * or the method timed-out.
 */
class fitting_prefix_aligner {
 public:
  fitting_prefix_aligner(int max_trailing_model_moves, int max_iterations, const common::execution_context& context);

  [[nodiscard]] trace_alignment_t operator()(const petri_net::petri_net_accessor& pn_accessor,
                                             std::span<const row_id> variant) const;

 private:
  int max_trailing_model_moves_{};
  int max_iterations_{};
  const common::execution_context& context_;
};

}  // namespace celonis::accelerator::operators::process::alignment::fitting_prefix
