#include "replay.h"

#include "modules/operators/process/bpmn/a_star/adjacency_graph_adaptor.h"
#include "modules/operators/process/bpmn/a_star/adjacency_graph_heuristic.h"
#include "modules/operators/process/bpmn/replay_types.h"
#include "modules/operators/process/petri_net/a_star/consistent_path_construction.h"
#include "modules/operators/process/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/petri_net/a_star/shared_types.h"

namespace celonis::accelerator::operators::process::bpmn::a_star {

namespace {

using petri_net::a_star::exit_code;

/**
 * This class exists because the iterative_a_star class accepts references as parameters.
 * Therefore, we must ensure that references are valid as long as the search object is alive
 */
class a_star_search_wrapper {
  using consistent_path_construction_t =
      petri_net::a_star::consistent_path_construction<marking_with_num_fired_tasks, transition,
                                                      adjacency_graph_heuristic::cost_type, marking_hash>;
  using iterative_a_star_t =
      petri_net::a_star::iterative_a_star<adjacency_graph_adaptor, adjacency_graph_heuristic,
                                          consistent_path_construction_t, marking_with_num_fired_tasks>;

 public:
  a_star_search_wrapper(const cpml::model::bpmn_graph& model, marking_t initial_marking,
                        cpml::model::bpmn::vertex_id_type target, const common::execution_context& context)
      : adaptor_{model, target},
        heuristic_{model, target},
        path_construction_{context},  // Each searcher has its own path construction object to keep track of the state
        initial_marking_{std::move(initial_marking), 0},
        searcher_{adaptor_, heuristic_, path_construction_, initial_marking_, a_star_max_cost} {}

  [[nodiscard]] iterative_a_star_t& searcher() { return searcher_; }

 private:
  static constexpr adjacency_graph_heuristic::cost_type a_star_max_cost{100};

  adjacency_graph_adaptor adaptor_;
  adjacency_graph_heuristic heuristic_;
  consistent_path_construction_t path_construction_;
  marking_with_num_fired_tasks initial_marking_;
  iterative_a_star_t searcher_;
};

template <typename SEARCHER>
[[nodiscard]] auto search(SEARCHER& searcher, int& max_iterations) {
  auto search_exit_code{exit_code::SEARCHING};
  for (; search_exit_code == exit_code::SEARCHING && max_iterations > 0; --max_iterations) {
    search_exit_code = searcher();
  }
  return search_exit_code;
}

template <typename SEARCHER>
[[nodiscard]] auto search(SEARCHER& searcher) {
  int max_iterations{1000};
  return search(searcher, max_iterations);
}

/**
 * When replaying a trace on the model we can either find the (linearized) transitions from start to end, or we did
 * not find a result.
 *
 * In the case where we did not find anything, we return the trace remainder together with a status
 * to indicate whether we have exhausted the search (nothing_found::AT_ALL) or whether we might be able to continue
 * searching further up the call stack (nothing_found::YET).
 */
using nothing_found = petri_net::a_star::nothing_found;
struct nothing_found_with_remainder {
  nothing_found status{};
  activity_trace_t trace_remainder{};
};
using replay_impl_return_t = std::variant<transitions_t, nothing_found_with_remainder>;

/**
 * Tries to find a path from the current marking to the end vertex of the BPMN model.
 *
 * @param model the BPMN model.
 * @param current_marking the marking after finding a path for all activities in the trace and firing the corresponding
 * transitions.
 */
[[nodiscard]] replay_impl_return_t try_finalize_trace(const cpml::model::bpmn_graph& model,
                                                      const marking_t& current_marking,
                                                      const common::execution_context& context) {
  // The trace is finalized once we reach the end marking
  if (end_marking_reached(model, current_marking)) {
    return transitions_t{};
  }

  // We still need to find a path to the end vertex, as we did not (yet) reach it
  a_star_search_wrapper search_wrapper{model, current_marking, model.single_end_vertex(), context};
  auto& searcher{search_wrapper.searcher()};
  if (search(searcher) != exit_code::FOUND_FIT) {
    // We did not find anything YET as we can still keep exploring other paths at a higher level in the call stack.
    return nothing_found_with_remainder{nothing_found::YET, {}};
  }
  return searcher.reconstruct().value();
}

template <typename SEARCHER>
[[nodiscard]] replay_impl_return_t try_reach_task(SEARCHER& searcher, int& max_iterations,
                                                  const activity_trace_t& trace) {
  if (const auto exit_code{search(searcher, max_iterations)}; exit_code != exit_code::FOUND_FIT) {
    if (exit_code == exit_code::DONE) {
      // We have exhausted the search and could not find any path
      return nothing_found_with_remainder{nothing_found::AT_ALL, trace};
    }
    // We did not find anything YET as we can still keep exploring other paths at a higher level in the call stack.
    return nothing_found_with_remainder{nothing_found::YET, trace};
  }
  return searcher.reconstruct().value();
}

/**
 * Replays the first activity in the trace on the model for a given initial marking. Afterwards, this function is
 * called recursively with the remainder of the trace.
 *
 * @param model the BPMN model.
 * @param initial_marking this is either the initial marking of the model, or the marking after finding and firing a
 * path in previous iterations of the search.
 * @param trace the trace (remainder) we want to replay.
 */
[[nodiscard]] replay_impl_return_t replay_trace_iterative_impl(const cpml::model::bpmn_graph& model,
                                                               const marking_t& initial_marking,
                                                               const activity_trace_t& trace,
                                                               const common::execution_context& context) {
  if (trace.empty()) {
    return try_finalize_trace(model, initial_marking, context);
  }

  const auto target_activity{trace.front()};
  const auto maybe_target_vertex{get_vertex_id_for_task(model, target_activity)};
  if (!maybe_target_vertex.has_value()) {
    // If activity is not in the model, then the trace does not conform and never will. The entire trace is the
    // non-conforming part
    return nothing_found_with_remainder{nothing_found::AT_ALL, trace};
  }
  const auto target{maybe_target_vertex.value()};

  // Every level in the recursion has it's own searcher instance
  // We declare these outside of try_reach_task below as we want to reuse these if we keep searching later on
  a_star_search_wrapper search_wrapper{model, initial_marking, target, context};
  auto& searcher{search_wrapper.searcher()};
  int max_iterations{1000};

  auto linearized_transitions_or_nothing_found{try_reach_task(searcher, max_iterations, trace)};
  if (std::holds_alternative<nothing_found_with_remainder>(linearized_transitions_or_nothing_found)) {
    // We were not able to reach the target, return the result up the call stack.
    // Maybe we can reach it through a different path.
    return linearized_transitions_or_nothing_found;
  }

  // We did find a path to the target
  debug_assert(std::holds_alternative<transitions_t>(linearized_transitions_or_nothing_found));
  auto linearized_transitions{std::get<transitions_t>(linearized_transitions_or_nothing_found)};

  marking_t candidate_marking{fire(initial_marking, linearized_transitions)};

  // We keep searching for other paths until we find something that finds a path for the trace remainder
  const activity_trace_t trace_remainder{{std::next(trace.cbegin()), trace.cend()}};
  auto remainder_search_result{replay_trace_iterative_impl(model, candidate_marking, trace_remainder, context)};
  while (std::holds_alternative<nothing_found_with_remainder>(remainder_search_result)) {
    // We keep searching
    if (search(searcher, max_iterations) != exit_code::FOUND_FIT) {
      // We exit the while loop as maybe another task transition yields a path
      break;
    }

    // We have found another path as an alternative
    linearized_transitions = searcher.reconstruct().value();

    // Update our candidate marking to account for the different path
    candidate_marking = fire(initial_marking, linearized_transitions);

    // Check if this path enables us to find a path for the trace remainder
    remainder_search_result = replay_trace_iterative_impl(model, candidate_marking, trace_remainder, context);
  }

  if (std::holds_alternative<nothing_found_with_remainder>(remainder_search_result)) {
    // We have not found a path but we might be able to continue our search further up the call stack
    return remainder_search_result;
  }

  // We did find a path for the remainder, so we add it to the linearized transitions
  debug_assert(std::holds_alternative<transitions_t>(remainder_search_result));
  const auto remainder_linearized_transitions{std::get<transitions_t>(remainder_search_result)};
  linearized_transitions.insert(linearized_transitions.cend(), remainder_linearized_transitions.cbegin(),
                                remainder_linearized_transitions.cend());

  return linearized_transitions;
}

}  // namespace

replay_return_t replay_trace(const cpml::model::bpmn_graph& model, const marking_t& initial_marking,
                             const activity_trace_t& trace, const common::execution_context& context) {
  const auto linearized_transitions_or_nothing_found{
      replay_trace_iterative_impl(model, initial_marking, trace, context)};

  if (std::holds_alternative<nothing_found_with_remainder>(linearized_transitions_or_nothing_found)) {
    // The trace does not conform to the model
    const auto trace_remainder{
        std::get<nothing_found_with_remainder>(linearized_transitions_or_nothing_found).trace_remainder};

    // In case the trace remainder is empty, we don't have a non-conforming subtrace
    if (trace_remainder.empty()) {
      return non_conforming_variant_t{trace};
    }

    // - 1 because we want the violating activity to be present in the non-conforming prefix
    return non_conforming_subtrace_t{
        {trace.cbegin(), std::prev(trace.cend(), static_cast<row_id>(trace_remainder.size() - 1))}};
  }

  // The trace does conform to the model
  debug_assert(std::holds_alternative<transitions_t>(linearized_transitions_or_nothing_found));
  return std::get<transitions_t>(linearized_transitions_or_nothing_found);
}

}  // namespace celonis::accelerator::operators::process::bpmn::a_star