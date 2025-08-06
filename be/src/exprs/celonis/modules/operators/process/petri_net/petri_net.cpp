#include "petri_net.h"

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "legacy_embedded_ctl/algorithm.h"
#include "legacy_embedded_ctl/assert.h"
#include "log/log.h"
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

namespace {

/**
 * For a given fork node, returns corresponding parallel section join node.
 * Initially marks all output places from node, then performs BFS to find transition T s.t. *T = cur_marking
 * - Check original code for a faster algorithm to compute splits
 */
petri_net_transition_id compute_closing_join(const petri_net_accessor& pn_accessor,
                                             const petri_net_transition& fork_transition, size_t max_depth) {
  // Initial marking are the output places of split
  auto current_marking{pn_accessor.get_marking(fork_transition.out_places)};

  std::unordered_set<marking_type, boost::hash<marking_type>, std::equal_to<>> visited{};
  std::deque<marking_type> to_expand{current_marking};
  std::deque<marking_type> next_round{};

  // Returns true if marking = *transition
  auto termination_condition_lmb{[&](const marking_type& marking, petri_net_transition_id transition) {
    return marking == pn_accessor.get_marking(pn_accessor[transition].in_places);
  }};
  auto already_visited_marking_lmb{[&](const marking_type& marking) { return visited.contains(marking); }};

  for (uint64_t current_depth{0}; current_depth != max_depth; ++current_depth) {
    if (to_expand.empty()) {
      break;
    }

    for (const auto& marking_to_expand : to_expand) {
      const auto enabled_transitions{pn_accessor.get_enabled_transitions(marking_to_expand)};

      for (const auto& enabled_transition : enabled_transitions) {
        if (termination_condition_lmb(marking_to_expand, enabled_transition)) {
          return enabled_transition;
        }

        const auto next_marking{pn_accessor.fire(marking_to_expand, enabled_transition)};
        if (!already_visited_marking_lmb(next_marking) && next_marking != current_marking) {
          next_round.emplace_back(next_marking);
          visited.insert(next_marking);
        }
      }
    }

    std::swap(to_expand, next_round);
    next_round.clear();
  }

  // It's not an error that a fork node has no corresponding join (it can happen in unfoldings for example)
  //  But report it because returning NULL can cause strange behavior later in the algorithm
  if (!to_expand.empty()) {
    // If the warning comes from here, then increasing BFS depth might fix it
    log::warn("{}: Couldn't find join transition after {} iterations.", pn_accessor.get_user_visible_operator_name(),
              max_depth);
  } else {
    log::warn("{}: Couldn't find join transition, setting to NULL.", pn_accessor.get_user_visible_operator_name());
  }
  return petri_net_transition_id::create_null_transition();
}

}  // namespace

bool petri_net_representation::is_reachable_from(const std::string& source_place,
                                                 const std::string& target_place) const {
  std::deque<std::string> to_open{source_place};
  std::unordered_set<std::string> visited{source_place};

  while (!to_open.empty()) {
    const auto visiting_node{to_open.front()};
    to_open.pop_front();

    for (auto [from_transition, to_transition] = place_transition_arcs.equal_range(visiting_node);
         from_transition != to_transition; ++from_transition) {
      for (auto [from_place, to_place] = transition_place_arcs.equal_range(from_transition->second);
           from_place != to_place; ++from_place) {
        if (from_place->second != target_place) {
          const auto place_not_visited_before{visited.emplace(from_place->second).second};
          if (place_not_visited_before) {
            to_open.emplace_back(from_place->second);
          }
        } else {
          return true;
        }
      }
    }
  }

  return false;
}

std::pair<size_t, size_t> petri_net_representation::count_labels() const {
  std::vector<label_type> labels{};
  labels.reserve(transitions.size());

  std::transform(std::cbegin(transitions), std::cend(transitions), std::back_inserter(labels),
                 [](const auto& entry) { return entry.second; });
  labels.erase(std::remove_if(std::begin(labels), std::end(labels), string_to_int_mapper::is_tau_transition),
               std::end(labels));
  const auto number_visible_transitions{labels.size()};

  std::sort(std::begin(labels), std::end(labels));
  // Remove duplicates
  labels.erase(std::unique(std::begin(labels), std::end(labels)), std::end(labels));
  const auto number_distinct_visible_labels{labels.size()};

  return {number_visible_transitions, number_distinct_visible_labels};
}

/**
 *   Reconnect cut-offs if needed
 *   For each leaf, find non-leaf with same originalNode ref
 *   Set has_path = exists path to original node from the non-leaf
 *      If !has_path, then is an IF. If add_if_cutoffs, then reconnect
 *      If has_path, then is a loop. If add_loop_cutoffs, then reconnect
 *  Mark leaf to be removed
 */
petri_net_representation reconnect_unfolding(const unfolding_representation& unf_repr,
                                             rl_align::unfolding_config unf_config) {
  petri_net_representation reconnected_unfolding{unf_repr.petri_net_repr};
  std::unordered_multimap<std::string, std::string> to_redirect{};

  for (const auto& leaf : unf_repr.leaves) {
    const auto& original_node_ref{unf_repr.condition_id_to_original_node_id.at(leaf)};

    for (auto [from, to] = unf_repr.original_node_id_to_condition_id.equal_range(original_node_ref); from != to;
         ++from) {
      const auto& node_with_same_name_id{from->second};
      if (unf_repr.leaves.count(node_with_same_name_id) == 0 && node_with_same_name_id != leaf) {
        const auto has_path{unf_repr.petri_net_repr.is_reachable_from(node_with_same_name_id, leaf)};
        if ((!has_path && unf_config.add_if_cutoffs) || (has_path && unf_config.add_loop_cutoffs)) {
          to_redirect.emplace(leaf, node_with_same_name_id);
        }
      }
    }
  }

  // Redirect arcs
  for (auto& [transition, place] : reconnected_unfolding.transition_place_arcs) {
    (void)transition;
    const auto tgt_place_to_redirect{to_redirect.find(place)};
    if (tgt_place_to_redirect != to_redirect.end()) {
      place = tgt_place_to_redirect->second;
    }
  }

  // Final Markings are the remaining leaf nodes after reconnecting cut-offs
  reconnected_unfolding.final_marking = unf_repr.leaves;
  for (const auto& [leaf, _] : to_redirect) {
    reconnected_unfolding.places.erase(leaf);
    reconnected_unfolding.final_marking.erase(leaf);
  }

  return reconnected_unfolding;
}

safe_petri_net_data::safe_petri_net_data(input_output_mapper& io_mapper, const petri_net_representation& pn_repr,
                                         std::string operator_name)
    : operator_name{std::move(operator_name)} {
  final_markings.reserve(pn_repr.final_marking.size());

  add_places(io_mapper, pn_repr);
  add_transitions(io_mapper, pn_repr);
  add_place_transition_arcs(io_mapper, pn_repr);
  add_transition_place_arcs(io_mapper, pn_repr);

  build_label_to_transitions_map();
}

marking_type petri_net_accessor::get_initial_marking() const { return pn_data_.initial_marking; }

std::vector<petri_net_place_id> petri_net_accessor::get_initial_places() const {
  std::vector<petri_net_place_id> ret{};

  for (const auto& pn_place : pn_data_.pn_places) {
    const auto id{pn_place.place.id};
    if (pn_data_.initial_marking.test(id)) {
      ret.emplace_back(pn_place.place);
    }
  }

  return ret;
}

bool petri_net_accessor::is_transition_enabled(const marking_type& marking, petri_net_transition_id transition) const {
  // Transition not in petri net
  if (transition.id >= pn_data_.pn_transitions.size()) {
    return false;
  }

  const auto& pn_transition{pn_data_.pn_transitions[transition.id]};

  return std::all_of(std::cbegin(pn_transition.in_places), std::cend(pn_transition.in_places),
                     [&marking](const auto& in_place) { return marking.test(in_place.id); });
}

marking_type petri_net_accessor::fire(const marking_type& marking, petri_net_transition_id transition) const {
  auto ret{marking};
  fire_no_alloc(ret, transition);
  return ret;
}

void petri_net_accessor::fire_no_alloc(marking_type& marking, petri_net_transition_id transition) const {
  const auto& pn_transition{pn_data_.pn_transitions[transition.id]};

  for (const auto& in_place : pn_transition.in_places) {
    legacy_embedded_debug_assert(marking.test(in_place.id));
    marking.reset(in_place.id);
  }

  for (const auto& out_place : pn_transition.out_places) {
    if (marking.test(out_place.id)) {
      throw common::cpm_exception{
          "{}: Petri Net is not safe. There exists a reachable marking with a place with more than 2 tokens.",
          get_user_visible_operator_name()};
    }
    marking.set(out_place.id);
  }
}

marking_type petri_net_accessor::fire_inverse(const marking_type& marking, petri_net_transition_id transition) const {
  auto ret{marking};
  fire_inverse_no_alloc(ret, transition);
  return ret;
}

void petri_net_accessor::fire_inverse_no_alloc(marking_type& marking, petri_net_transition_id transition) const {
  const auto& pn_transition{pn_data_.pn_transitions[transition.id]};

  for (const auto& out_place : pn_transition.out_places) {
    legacy_embedded_debug_assert(marking.test(out_place.id));
    marking.reset(out_place.id);
  }

  for (const auto& in_place : pn_transition.in_places) {
    legacy_embedded_debug_assert(!marking.test(in_place.id));
    marking.set(in_place.id);
  }
}

petri_net_accessor::transition_list_type compute_enabled_transitions(
    const std::vector<petri_net_transition>& transitions, const marking_type& marking) {
  petri_net_accessor::transition_list_type enabled_transitions{};
  for (const auto& pn_transition : transitions) {
    if (pn_transition.transition.is_null()) {
      continue;
    }

    bool enabled{true};
    for (auto in_place : pn_transition.in_places) {
      if (!marking.test(in_place.id)) {
        enabled = false;
        break;
      }
    }
    if (enabled) {
      enabled_transitions.emplace_back(pn_transition.transition);
    }
  }

  return enabled_transitions;
}

petri_net_accessor::transition_span_type petri_net_accessor::get_enabled_transitions(
    const marking_type& marking) const {
  const auto cached_result{enabled_transitions_cache_.find(marking)};
  if (cached_result != enabled_transitions_cache_.end()) {
    return transition_span_type(begin(cached_result->second), end(cached_result->second));
  }

  auto enabled_transitions{compute_enabled_transitions(pn_data_.pn_transitions, marking)};
  auto [entry, _]{enabled_transitions_cache_.emplace(marking, tracked_transition_list_type{})};
  entry->second.reserve(enabled_transitions.size());
  entry->second.insert(std::begin(entry->second), std::begin(enabled_transitions), std::end(enabled_transitions));

  return transition_span_type{std::begin(entry->second), std::end(entry->second)};
}

size_t petri_net_accessor::get_max_transition_id() const { return pn_data_.pn_transitions.size(); }

marking_type petri_net_accessor::get_marking(const std::vector<petri_net_place_id>& set_places) const {
  marking_type marking{pn_data_.pn_places.size()};
  for (const auto& set_place : set_places) {
    marking.set(set_place.id);
  }

  return marking;
}

const std::vector<petri_net_transition>& petri_net_accessor::get_transitions() const { return pn_data_.pn_transitions; }

std::vector<petri_net_transition_id> petri_net_accessor::consecutive_transitions(
    const petri_net_transition& transition) const {
  std::vector<petri_net_transition_id> ret{};

  for (const auto& place : pn_data_.pn_transitions[transition.transition.id].out_places) {
    for (const auto& out_transition : pn_data_.pn_places[place.id].out_transitions) {
      ret.emplace_back(out_transition);
    }
  }

  return ret;
}

[[nodiscard]] bool petri_net_accessor::is_fork_node(petri_net_transition_id transition) const {
  return pn_data_.pn_transitions[transition.id].out_places.size() > 1;
}

const std::vector<marking_type>& petri_net_accessor::get_final_markings() const { return pn_data_.final_markings; }

bool petri_net_accessor::is_final_marking(const marking_type& marking) const {
  return legacy_embedded_ctl::contains(pn_data_.final_markings, marking);
}

petri_net_accessor::transition_list_type petri_net_accessor::get_transitions_for_label(row_id label) const {
  transition_list_type ret{};

  for (auto [begin, end] = pn_data_.label_to_transitions.equal_range(label); begin != end; ++begin) {
    ret.emplace_back(begin->second);
  }

  return ret;
}

std::vector<petri_net_transition_id> petri_net_accessor::compatible_generating_transitions(
    const marking_type& marking) const {
  constexpr auto cmp{[](auto lhs, auto rhs) { return lhs.id < rhs.id; }};
  auto final_places{get_marked_place_ids(marking)};
  std::sort(begin(final_places), end(final_places), cmp);
  std::vector<petri_net_place_id> buffer{};
  std::vector<petri_net_transition_id> result{};
  for (const auto& transition : pn_data_.pn_transitions) {
    buffer.clear();
    std::copy(begin(transition.out_places), end(transition.out_places), back_inserter(buffer));
    std::sort(begin(buffer), end(buffer), cmp);
    if (std::includes(begin(final_places), end(final_places), begin(buffer), end(buffer), cmp)) {
      result.emplace_back(transition.transition);
    }
  }
  return result;
}
std::vector<petri_net_place_id> petri_net_accessor::get_marked_place_ids(const marking_type& marking) {
  std::vector<petri_net_place_id> result{};
  for (auto i{0u}; i != marking.size(); ++i) {
    if (marking.test(i)) {
      result.emplace_back(i);
    }
  }
  return result;
}

petri_net_accessor::transition_list_type petri_net_accessor::get_silent_enabled_transitions(
    const marking_type& marking) const {
  // TODO(a.swoboda) there's probably a faster low-level implementation, but at least this is simple
  const auto enabled_transitions{get_enabled_transitions(marking)};

  transition_list_type result{};
  result.reserve(enabled_transitions.size());
  std::copy_if(
      std::begin(enabled_transitions), std::end(enabled_transitions), std::back_inserter(result),
      [this](auto transition_id) { return !string_to_int_mapper::is_tau_transition(get_label(transition_id)); });

  return result;
}

void safe_petri_net_data::add_places(input_output_mapper& io_mapper, const petri_net_representation& pn_repr) {
  std::vector<std::pair<petri_net_place_id, std::string>> places_to_add{};
  places_to_add.reserve(pn_repr.places.size());
  for (const auto& place_str_id : pn_repr.places) {
    const auto place{io_mapper.add_place_for_str_id(place_str_id)};
    places_to_add.emplace_back(place, place_str_id);
  }

  // Now io_mapper has seen all places
  pn_places = std::vector<petri_net_place>(io_mapper.get_max_place_id(), petri_net_place{});
  initial_marking = marking_type{io_mapper.get_max_place_id()};
  for (const auto& [place_to_add, place_str_id] : places_to_add) {
    pn_places[place_to_add.id].place = place_to_add;
    pn_places[place_to_add.id].place_str_id = place_str_id;
  }

  for (const auto& start_place_str_id : pn_repr.initial_marking) {
    const auto place{io_mapper.add_place_for_str_id(start_place_str_id)};
    // Only false if start_place_str_id not in pn_repr.places, which means the network is ill formed
    legacy_embedded_debug_assert(place.id < pn_places.size());
    initial_marking.set(place.id);
  }

  for (const auto& final_place_str_id : pn_repr.final_marking) {
    const auto place{io_mapper.add_place_for_str_id(final_place_str_id)};
    // Only false if final_place_str_id not in pn_repr.places, which means the network is ill formed
    legacy_embedded_debug_assert(place.id < pn_places.size());
    marking_type final_marking{pn_places.size()};
    final_marking.set(place.id);
    final_markings.emplace_back(final_marking);
  }
}

void safe_petri_net_data::add_transitions(input_output_mapper& io_mapper, const petri_net_representation& pn_repr) {
  std::vector<std::tuple<petri_net_transition_id, row_id, std::string>> transitions_to_add{};
  transitions_to_add.reserve(pn_repr.transitions.size() + 1);
  // Null transition
  transitions_to_add.emplace_back(petri_net_transition_id::create_null_transition(),
                                  string_to_int_mapper::get_tau_transition_id(), "");

  // In order to achieve stable output, we sort the transitions
  std::vector<std::pair<std::string, label_type>> sorted_transitions(begin(pn_repr.transitions),
                                                                     end(pn_repr.transitions));
  std::ranges::sort(sorted_transitions);
  for (const auto& [transition_str_id, label] : sorted_transitions) {
    const auto transition{io_mapper.add_transition_for_str_id(transition_str_id)};
    transitions_to_add.emplace_back(transition, label, transition_str_id);
  }

  // Now io_mapper has seen all transitions
  pn_transitions = std::vector<petri_net_transition>(io_mapper.get_max_transition_id(), petri_net_transition{});
  for (const auto& [transition_to_add, label, transition_str_id] : transitions_to_add) {
    pn_transitions[transition_to_add.id].transition = transition_to_add;
    pn_transitions[transition_to_add.id].label = label;
    pn_transitions[transition_to_add.id].transition_str_id = transition_str_id;
  }
}

void safe_petri_net_data::add_place_transition_arcs(const input_output_mapper& io_mapper,
                                                    const petri_net_representation& pn_repr) {
  for (const auto& [from_place_str_id, to_transition_str_id] : pn_repr.place_transition_arcs) {
    const auto from_place{io_mapper.get_place_for_str_id(from_place_str_id)};
    const auto to_transition{io_mapper.get_transition_for_str_id(to_transition_str_id)};
    pn_places[from_place.id].out_transitions.emplace_back(to_transition);
    pn_transitions[to_transition.id].in_places.emplace_back(from_place);
  }
}

void safe_petri_net_data::add_transition_place_arcs(const input_output_mapper& io_mapper,
                                                    const petri_net_representation& pn_repr) {
  for (const auto& [from_transition_str_id, to_place_str_id] : pn_repr.transition_place_arcs) {
    const auto from_transition{io_mapper.get_transition_for_str_id(from_transition_str_id)};
    const auto to_place{io_mapper.get_place_for_str_id(to_place_str_id)};
    pn_transitions[from_transition.id].out_places.emplace_back(to_place);
  }
}

void safe_petri_net_data::build_label_to_transitions_map() {
  for (const auto& pn_transition : pn_transitions) {
    label_to_transitions.emplace(pn_transition.label, pn_transition.transition);
  }
}

petri_net_accessor_with_parallel_sections::petri_net_parallel_sections
petri_net_accessor_with_parallel_sections::petri_net_parallel_sections::compute(const petri_net_accessor& pn_accessor) {
  petri_net_parallel_sections ret{};

  for (const auto& pn_transition : pn_accessor.get_transitions()) {
    if (pn_accessor.is_fork_node(pn_transition.transition)) {
      // Find corresponding (enclosing) join (is there always one?)
      auto join{compute_closing_join(pn_accessor, pn_transition, pn_accessor.get_transitions().size())};
      ret.parallel_sections_.emplace(pn_transition.transition, join);
    }
  }
  return ret;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
