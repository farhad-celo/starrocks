#pragma once

#include <optional>
#include <unordered_set>
#include <vector>

#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

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
[[nodiscard]] std::optional<trace_alignment> align_fitting_prefix(petri_net::petri_net_accessor& pn_accessor,
                                                                  std::span<const row_id> variant,
                                                                  const common::execution_context& context,
                                                                  int max_trailing_model_moves = 100,
                                                                  int iterations = 1000);

[[nodiscard]] bool is_petri_net_run(petri_net::petri_net_accessor& pn_accessor_tt, const std::vector<row_id>& trace,
                                    const common::execution_context& context, int max_iterations = 1000);

}  // namespace celonis::accelerator::operators::process::alignment
