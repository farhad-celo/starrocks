#include "gap_filler.h"

#include <algorithm>

#include "legacy_embedded_ctl/conversion.h"
#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/petri_net/a_star/inconsistent_path_construction.h"
#include "modules/operators/process/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/petri_net/a_star/petri_net_wrapper.h"
#include "modules/operators/process/petri_net/a_star/shortest_path_heuristic.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

trace_alignment_t gap_filler::complete_alignment(const petri_net::rl_problem_solution_t& solution,
                                                 std::span<const row_id> trace, int max_cost,
                                                 const common::execution_context& context) {
  trace_alignment alignment{};
  auto current_marking{pn_accessor_.get_initial_marking()};
  if (!complete_to_sync_moves(solution, alignment, current_marking, trace, max_cost, context)) {
    return std::nullopt;
  }
  if (!complete_to_final_marking(alignment, current_marking, max_cost, context)) {
    return std::nullopt;
  }
  return alignment;
}

bool gap_filler::complete_to_sync_moves(const petri_net::rl_problem_solution_t& solution, trace_alignment& alignment,
                                        petri_net::marking_type& current_marking, std::span<const row_id> trace,
                                        int max_cost, const common::execution_context& context) {
  // Insert missing model moves to enable next synchronous move
  // If there is no way to enable the target synchronous move, make it into a log move and continue
  for (const auto& [variable, transition] : solution) {
    const auto remaining_cost_budget{max_cost - legacy_embedded_ctl::cast<int>(alignment.cost_optimistic_lcs_pruning())};
    if (remaining_cost_budget <= 0) {
      return false;
    }
    const auto activity_id{trace[variable.id]};

    // It is cleaner to repeat this branch than the way it was written before
    // NOLINTNEXTLINE(bugprone-branch-clone,-warnings-as-errors,-warnings-as-errors)
    if (bool is_log_move{transition.is_null()}; is_log_move) {
      alignment.add(alignment_move::log(activity_id));
    } else if (pn_accessor_.is_transition_enabled(current_marking, transition)) {
      // Is synchronous transition and is enabled
      pn_accessor_.fire_no_alloc(current_marking, transition);
      alignment.add(alignment_move::sync(activity_id, transition));
    } else if (auto maybe_enabling_transitions{
                   get_enabling_transitions_to(current_marking, transition, remaining_cost_budget, context)};
               maybe_enabling_transitions) {
      const auto& enabling_transitions{maybe_enabling_transitions.value()};
      for (const auto enabling_transition : enabling_transitions) {
        const auto enabling_activity_id{pn_accessor_.get_label(enabling_transition)};
        alignment.add(alignment_move::model(enabling_activity_id, enabling_transition));
        pn_accessor_.fire_no_alloc(current_marking, enabling_transition);
      }
      pn_accessor_.fire_no_alloc(current_marking, transition);
      alignment.add(alignment_move::sync(activity_id, transition));
    } else {
      // Is sync move, but we can't enable it => Turn into model and proceed
      alignment.add(alignment_move::log(activity_id));
    }
  }
  return true;
}

gap_filler::maybe_transition_span_type gap_filler::get_enabling_transitions_to(
    const petri_net::marking_type& current_marking, petri_net::petri_net_transition_id transition, int max_cost,
    const common::execution_context& context) {
  paths_to_transition_cache_type::key_t key{current_marking, transition};
  auto [entry, inserted]{paths_to_transition.entries.emplace(key, std::nullopt)};

  if (inserted) {
    using cost_type = petri_net::a_star::heuristic_to_transition::cost_type;
    auto max_a_star_cost{std::min(legacy_embedded_ctl::cast<cost_type>(max_cost), max_insertions_)};

    auto heuristic{petri_net::a_star::heuristic_to_transition{transition, transition_distances_, pn_accessor_}};
    using path_construction_t =
        petri_net::a_star::inconsistent_path_construction<petri_net::marking_type, petri_net::petri_net_transition_id,
                                                          cost_type, boost::hash<petri_net::marking_type>>;
    auto search_result{petri_net::a_star::a_star_search(petri_net::a_star::petri_net_wrapper{pn_accessor_}, heuristic,
                                                        path_construction_t{context}, current_marking, max_a_star_cost,
                                                        max_iterations_)};

    using transition_list_type = petri_net::a_star::petri_net_wrapper::transition_list_type;
    transition_list_type enabling_transitions{};
    if (std::holds_alternative<transition_list_type>(search_result)) {
      const auto& transitions{std::get<transition_list_type>(search_result)};
      entry->second = paths_to_transition_cache_type::tracked_path_t(std::cbegin(transitions), std::cend(transitions));
    }
  }

  return entry->second;
}

bool gap_filler::complete_to_final_marking(trace_alignment& alignment, petri_net::marking_type& current_marking,
                                           int max_cost, const common::execution_context& context) {
  const auto& final_markings{pn_accessor_.get_final_markings()};
  if (pn_accessor_.is_final_marking(current_marking)) {
    return true;
  }
  const auto remaining_cost_budget{max_cost - legacy_embedded_ctl::cast<int>(alignment.cost_optimistic_lcs_pruning())};
  if (remaining_cost_budget <= 0) {
    return false;
  }
  for (const auto& final_marking : final_markings) {
    auto maybe_enabling_transitions{
        get_enabling_transitions_to(current_marking, final_marking, remaining_cost_budget, context)};
    if (!maybe_enabling_transitions) {
      continue;
    }

    const auto& enabling_transitions{maybe_enabling_transitions.value()};
    auto extended_alignment{alignment};
    for (const auto enabling_transition : enabling_transitions) {
      const auto activity_id{pn_accessor_.get_label(enabling_transition)};
      extended_alignment.add(alignment_move::model(activity_id, enabling_transition));
    }
    extended_alignment.prune_simple();
    if (max_cost <= legacy_embedded_ctl::cast<int>(extended_alignment.cost_optimistic_lcs_pruning())) {
      continue;
    }
    // else, we have found an alignment that is better than the fallback
    // as a future improvement, we could first check all final markings and return the best if there is more than one
    alignment = std::move(extended_alignment);
    for (const auto& transition : enabling_transitions) {
      pn_accessor_.fire_no_alloc(current_marking, transition);
    }
    return true;
  }
  return false;
}

gap_filler::maybe_transition_span_type gap_filler::get_enabling_transitions_to(
    const petri_net::marking_type& current_marking, const petri_net::marking_type& final_marking, int max_cost,
    const common::execution_context& context) {
  paths_to_marking_cache_type::key_t key{current_marking, final_marking};
  auto [entry, inserted]{paths_to_marking.entries.emplace(key, std::nullopt)};

  if (inserted) {
    using cost_type = petri_net::a_star::heuristic_to_transition::cost_type;
    auto max_a_star_cost{std::min(max_cost, max_insertions_)};

    auto heuristic{petri_net::a_star::heuristic_to_marking{final_marking, transition_distances_, pn_accessor_}};
    auto path_construction{
        petri_net::a_star::inconsistent_path_construction<petri_net::marking_type, petri_net::petri_net_transition_id,
                                                          cost_type, boost::hash<petri_net::marking_type>>{context}};
    auto search_result{petri_net::a_star::a_star_search(pn_accessor_, heuristic, path_construction, current_marking,
                                                        max_a_star_cost, max_iterations_)};

    using transition_list_type = petri_net::a_star::petri_net_wrapper::transition_list_type;
    if (std::holds_alternative<transition_list_type>(search_result)) {
      const auto& transitions{std::get<transition_list_type>(search_result)};
      entry->second = paths_to_marking_cache_type::tracked_path_t(std::cbegin(transitions), std::cend(transitions));
    }
  }

  return entry->second;
}

}  // namespace celonis::accelerator::operators::process::alignment::rl_align