#pragma once

#include <cpml/model/bpmn_graph_fwd.h>

#include "modules/common/execution_context_fwd.h"
#include "modules/operators/process/bpmn/replay_types.h"

/**
 * REPLAY STRATEGY FOR BPMN MODELS
 *
 * Traces are replayed on BPMN models by making use of an A* search implementation. The replay algorithm tries to find
 * a path to task node corresponding to the next activity in the trace in a recursive manner. Every level in the
 * recursion tries to find a path to the target task vertex by firing gateways and the target vertex.
 *
 * The algorithm additionally implements backtracking. If at any point during the search no path can be found, this is
 * returned up the call stack and the previous level continues searching to find a different path to the its target
 * vertex. This is helpful for self looping activities and for models where there are multiple paths to the same task
 * vertex.
 *
 */
namespace celonis::accelerator::operators::process::bpmn::a_star {

using non_conforming_subtrace_t = trace_t;
using non_conforming_variant_t = activity_trace_t;
using replay_return_t = std::variant<transitions_t, non_conforming_subtrace_t, non_conforming_variant_t>;

/**
 * Tries to replay the trace on the model given the initial marking. Uses a_star to find a path to each task vertex
 * as given in the trace. If no path can be found, backtracking is used to try and find a different path which might
 * lead to a successful replay.
 *
 * @param model the model on which we want to replay.
 * @param initial_marking the initial marking for the search (most likely this is the initial marking of the model as
 * well, but you could provide a different start marking).
 * @param trace the trace that is to be replayed.
 * @return a variant containing either the linearized transitions (if a path was found) or the subtrace that was
 * non-conforming
 */
[[nodiscard]] replay_return_t replay_trace(const cpml::model::bpmn_graph& model, const marking_t& initial_marking,
                                           const activity_trace_t& trace, const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::bpmn::a_star