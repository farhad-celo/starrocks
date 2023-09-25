#include "modules/operators/process/alignment/gap_filler.h"

#include <vector>

#include "ctl/conversion.h"
#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/alignment/petri_net/a_star/inconsistent_path_construction.h"
#include "modules/operators/process/alignment/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/alignment/petri_net/a_star/petri_net_wrapper.h"
#include "modules/operators/process/alignment/petri_net/a_star/shortest_path_heuristic.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

trace_alignment gap_filler::complete_alignment(const petri_net::rl_problem_solution_t& solution,
                                               std::span<const row_id> trace,
                                               const common::execution_context& context) {
  trace_alignment alignment{};

  auto current_marking{pn_accessor_.get_initial_marking()};
  std::optional<trace_alignment> fallback_alignment{};
  if (baseline_.has_value()) {
    auto fallback_value{baseline_.value()(trace, context)};
    if (solution.empty()) {
      return fallback_value;
    }
    fallback_alignment.emplace(std::move(fallback_value));
  }
  const auto max_cost{fallback_alignment.has_value() ? fallback_alignment.value().cost() : max_depth_};
  complete_to_sync_moves(solution, alignment, current_marking, trace, max_cost, context);
  bool is_complete{complete_to_final_marking(alignment, current_marking, max_cost, context)};

  return is_complete || !fallback_alignment.has_value() ? alignment : fallback_alignment.value();
}

void gap_filler::complete_to_sync_moves(const petri_net::rl_problem_solution_t& solution, trace_alignment& alignment,
                                        petri_net::marking_type& current_marking, std::span<const row_id> trace,
                                        uint64_t fallback_cost, const common::execution_context& context) {
  // Insert missing model moves to enable next synchronous move
  // If there is no way to enable the target synchronous move, make it into a log move and continue
  const auto num_log_moves{std::ranges::count_if(solution, [](const auto& t) { return t.tgt_transition.is_null(); })};
  // We may be able to combine log moves with model moves, so we need to add those to the maximum cost
  const auto max_cost{fallback_cost + 2 * num_log_moves};  // add twice to also account for the un-purged alignment cost
  for (const auto& [variable, transition] : solution) {
    if (alignment.cost() >= max_cost) {
      return;
    }
    bool log_move{transition.is_null()};

    if (!log_move && !pn_accessor_.is_transition_enabled(current_marking, transition)) {
      auto enabling_transitions{get_enabling_transitions_to(alignment, current_marking, max_cost, transition, context)};
      for (const auto enabling_transition : enabling_transitions) {
        const auto activity_id{pn_accessor_.get_label(enabling_transition)};
        alignment.add(alignment_move::model(activity_id, enabling_transition));
        pn_accessor_.fire_transition_no_alloc(current_marking, enabling_transition);
      }
      log_move = log_move || enabling_transitions.empty();
    }

    const auto activity_id{trace[variable.id]};
    if (log_move) {
      alignment.add(alignment_move::log(activity_id));
    } else {
      pn_accessor_.fire_transition_no_alloc(current_marking, transition);
      alignment.add(alignment_move::sync(activity_id, transition));
    }
  }
}

gap_filler::transition_list_type gap_filler::get_enabling_transitions_to(const trace_alignment& alignment,
                                                                         const petri_net::marking_type& current_marking,
                                                                         uint64_t max_cost,
                                                                         petri_net::petri_net_transition_id transition,
                                                                         const common::execution_context& context) {
  auto [in_cache, enabling_transitions] = path_cache_.path_to_transition(current_marking, transition);
  if (!in_cache) {
    using cost_type = petri_net::a_star::heuristic_to_transition::cost_type;
    auto heuristic{petri_net::a_star::heuristic_to_transition{transition, source_distances_, pn_accessor_}};
    using path_construction_t =
        petri_net::a_star::inconsistent_path_construction<petri_net::marking_type, petri_net::petri_net_transition_id,
                                                          cost_type, boost::hash<petri_net::marking_type>>;
    auto search_result{petri_net::a_star::a_star_search(
        petri_net::a_star::petri_net_wrapper{pn_accessor_}, heuristic, path_construction_t{context}, current_marking,
        ctl::cast<cost_type>(max_cost - alignment.cost()), MAX_ITERATIONS)};
    using transitions_type = petri_net::a_star::petri_net_wrapper::transition_list_type;
    if (std::holds_alternative<transitions_type>(search_result)) {
      enabling_transitions = std::get<transitions_type>(search_result);
    }
    // We insert the path even if we didn't find anything, so that we don't have to repeat the search
    path_cache_.insert_path_to_transition(current_marking, transition, enabling_transitions);
  }
  return enabling_transitions;
}

bool gap_filler::complete_to_final_marking(trace_alignment& alignment, petri_net::marking_type& current_marking,
                                           uint64_t fallback_cost, const common::execution_context& context) {
  const auto& final_markings{pn_accessor_.get_final_markings()};
  if (pn_accessor_.is_final_marking(current_marking)) {
    return true;
  }
  const auto num_log_moves{std::ranges::count_if(alignment.data(), &alignment_move::is_log)};
  const auto max_cost{fallback_cost + 2 * num_log_moves};
  if (alignment.cost() >= max_cost) {
    return false;
  }
  for (const auto& final_marking : final_markings) {
    auto enabling_transitions{
        get_enabling_transitions_to(alignment, current_marking, max_cost, final_marking, context)};
    auto extended_alignment{alignment};
    for (const auto enabling_transition : enabling_transitions) {
      const auto activity_id{pn_accessor_.get_label(enabling_transition)};
      extended_alignment.add(alignment_move::model(activity_id, enabling_transition));
    }
    extended_alignment.prune_simple();
    if (enabling_transitions.empty() || fallback_cost <= extended_alignment.cost()) {
      continue;
    }
    // else, we have found an alignment that is better than the fallback
    // as a future improvement, we could first check all final markings and return the best if there is more than one
    alignment = std::move(extended_alignment);
    for (const auto& transition : enabling_transitions) {
      pn_accessor_.fire_transition_no_alloc(current_marking, transition);
    }
    return true;
  }
  return false;
}

gap_filler::transition_list_type gap_filler::get_enabling_transitions_to(const trace_alignment& alignment,
                                                                         const petri_net::marking_type& current_marking,
                                                                         uint64_t max_cost,
                                                                         const petri_net::marking_type& final_marking,
                                                                         const common::execution_context& context) {
  auto [in_cache, enabling_transitions]{path_cache_.path_to_marking(current_marking, final_marking)};
  if (!in_cache) {
    using cost_type = petri_net::a_star::heuristic_to_transition::cost_type;
    auto heuristic{petri_net::a_star::heuristic_to_marking{final_marking, source_distances_, pn_accessor_}};
    auto path_construction{
        petri_net::a_star::inconsistent_path_construction<petri_net::marking_type, petri_net::petri_net_transition_id,
                                                          cost_type, boost::hash<petri_net::marking_type>>{context}};
    auto search_result{petri_net::a_star::a_star_search(
        petri_net::a_star::petri_net_wrapper{pn_accessor_}, heuristic, path_construction, current_marking,
        ctl::cast<cost_type>(max_cost - alignment.cost()), MAX_ITERATIONS)};

    using transitions_type = petri_net::a_star::petri_net_wrapper::transition_list_type;
    if (std::holds_alternative<transitions_type>(search_result)) {
      enabling_transitions = std::get<transitions_type>(search_result);
    }
    // We insert the path even if we didn't find anything, so that we don't have to repeat the search
    path_cache_.insert_path_to_marking(current_marking, final_marking, enabling_transitions);
  }
  return enabling_transitions;
}

}  // namespace celonis::accelerator::operators::process::alignment