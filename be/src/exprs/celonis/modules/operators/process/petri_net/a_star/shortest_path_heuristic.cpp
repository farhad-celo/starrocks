#include "shortest_path_heuristic.h"

#include <algorithm>

#include "modules/operators/process/alignment/input_output_mapper.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

namespace {

transition_distances_matrix::distance_type heuristic_min(transition_distances_matrix::distance_type lhs,
                                                         transition_distances_matrix::distance_type rhs) {
  return lhs == transition_distances_matrix::UNCONNECTED   ? rhs
         : rhs == transition_distances_matrix::UNCONNECTED ? lhs
                                                           : std::min(lhs, rhs);
}

template <typename ITERATOR_FIRST, typename ITERATOR_LAST>
std::vector<petri_net_transition_id> get_all_enabling_transitions(ITERATOR_FIRST first, ITERATOR_LAST last,
                                                                  const petri_net_accessor& petri_net) {
  std::vector<petri_net_transition_id> result{};
  for (; first != last; ++first) {
    const auto compatible_transitions{petri_net.compatible_generating_transitions(*first)};
    result.insert(end(result), begin(compatible_transitions), end(compatible_transitions));
  }
  constexpr auto less{[](petri_net_transition_id lhs, petri_net_transition_id rhs) { return lhs.id < rhs.id; }};
  std::sort(begin(result), end(result), less);
  result.erase(std::unique(begin(result), end(result)), end(result));
  return result;
}

template <typename FIRST, typename LAST>
[[nodiscard]] std::optional<transition_distances_matrix::distance_type> transitions_estimate(
    FIRST first, LAST last, const marking_type& marking, const transition_distances_matrix& metric,
    const petri_net_accessor& petri_net) {
  const auto distance_min{[&metric, &petri_net, &marking](const auto& acc, auto transition) {
    auto distance_to_transition{heuristic_to_transition{transition, metric, petri_net}.estimate(marking)};
    if (distance_to_transition.has_value()) {
      const auto transition_weight{string_to_int_mapper::is_tau_transition(petri_net.get_label(transition)) ? 0 : 1};
      distance_to_transition.value() += transition_weight;
    }

    return std::min(acc, distance_to_transition,
                    [](const std::optional<heuristic_to_markings::cost_type>& lhs,
                       const std::optional<heuristic_to_markings::cost_type>& rhs) {
                      // an empty optional means "unconnected", so contrary to std::optional::operator<,
                      // it is greater (not less) than any other value
                      return (lhs.has_value() && !rhs.has_value()) || (lhs.has_value() && lhs.value() < rhs.value());
                    });
  }};
  return std::accumulate(first, last, std::optional<transition_distances_matrix::distance_type>{}, distance_min);
}

}  // namespace

heuristic_to_transition::cost_type heuristic_to_transition::get_weight(transition_type transition) const {
  return string_to_int_mapper::is_tau_transition(petri_net_.get_label(transition)) ? 0 : 1;
}

std::optional<heuristic_to_transition::cost_type> heuristic_to_transition::estimate(
    const petri_net::marking_type& marking) const {
  auto estimate{transition_distances_matrix::UNCONNECTED};
  for (const auto& from : petri_net_.get_enabled_transitions(marking)) {
    if (from == target_) {
      return {0};
    }
    estimate = heuristic_min(estimate, transition_metric_.get_distance(from, target_));
  }
  if (estimate == transition_distances_matrix::UNCONNECTED) {
    return {};
  }
  return {estimate};
}

heuristic_to_marking::cost_type heuristic_to_marking::get_weight(transition_type transition) const {
  return string_to_int_mapper::is_tau_transition(petri_net_.get_label(transition)) ? 0 : 1;
}

std::optional<heuristic_to_marking::cost_type> heuristic_to_marking::estimate(
    const petri_net::marking_type& marking) const {
  if (marking == target_) {
    return {0};
  }
  return transitions_estimate(begin(target_transitions_), end(target_transitions_), marking, transition_distances_,
                              petri_net_);
}

heuristic_to_markings::heuristic_to_markings(std::vector<petri_net::marking_type> targets,
                                             const transition_distances_matrix& transition_distances,
                                             const petri_net_accessor& petri_net)
    : target_markings_{std::move(targets)},
      target_transitions_{get_all_enabling_transitions(begin(target_markings_), end(target_markings_), petri_net)},
      metric_{transition_distances},
      petri_net_{petri_net} {}

heuristic_to_markings::cost_type heuristic_to_markings::get_weight(transition_type transition) const {
  return string_to_int_mapper::is_tau_transition(petri_net_.get_label(transition)) ? 0 : 1;
}

std::optional<heuristic_to_markings::cost_type> heuristic_to_markings::estimate(
    const petri_net::marking_type& marking) const {
  if (legacy_embedded_ctl::contains(target_markings_, marking)) {
    return {0};
  }
  return transitions_estimate(begin(target_transitions_), end(target_transitions_), marking, metric_, petri_net_);
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
