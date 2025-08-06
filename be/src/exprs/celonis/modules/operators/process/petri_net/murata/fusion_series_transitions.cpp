#include "fusion_series_transitions.h"

#include <algorithm>
#include <optional>
#include <unordered_set>

#include "modules/operators/process/petri_net/murata/reduction_rules_utils.h"
#include "modules/operators/process/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

std::optional<fusion_series_transition_and_tau> fusion_series_transition_and_tau::try_build(
    const petri_net_builder& pn, const std::unordered_set<node_id_type>& keep_transitions,
    const fusion_series_transition_and_tau::node_id_type& node_id) {
  // The node is a place
  if (!pn.is_place(node_id)) {
    return std::nullopt;
  }
  const node_id_type& place{node_id};

  // The place is SESE with the same arc weights = 1
  const auto is_sese{get_if_single_entry_single_exit_with_weight(pn, place, 1)};
  if (!is_sese.has_value()) {
    return std::nullopt;
  }
  const auto& [in_transition, out_transition]{is_sese.value()};

  //  The place's output transitions is not "keep"
  if (keep_transitions.contains(out_transition)) {
    return std::nullopt;
  }

  //  The output transition has only one pre_condition (the place)
  if (pn.pre_set(out_transition) != std::unordered_set<node_id_type>{place}) {
    return std::nullopt;
  }

  //  The input and output transitions are distinct
  if (in_transition == out_transition) {
    return std::nullopt;
  }

  //  The place is not part of the final marking
  if (pn.is_final_place(place)) {
    return std::nullopt;
  }

  // BECAUSE WE REPRESENT A MARKING AS BITSET:
  const auto out_transition_post_set{pn.post_set(out_transition)};
  //  The post-sets of the input and output transitions are disjoint
  //    (it is surprising that C++ doesn't natively support set operations)
  const auto is_input_transition_post_set_f{
      [post_set = pn.post_set(in_transition)](const auto& p) { return post_set.contains(p); }};
  if (std::ranges::any_of(out_transition_post_set, is_input_transition_post_set_f)) {
    return std::nullopt;
  }

  //  If the place is part of the initial marking, then the output places of its output transition are not
  const auto is_initial_place_f{[&pn = std::as_const(pn)](const auto& p) { return pn.is_initial_place(p); }};
  if (pn.is_initial_place(place) && std::ranges::any_of(out_transition_post_set, is_initial_place_f)) {
    return std::nullopt;
  }

  return fusion_series_transition_and_tau{in_transition, place, out_transition};
}

void fusion_series_transition_and_tau::apply(petri_net_builder& pn) const {
  // Initial counts for the place
  auto place_initial_count{pn.is_initial_place(place_) ? pn.at_place(place_).initial_marking_counts : 0};

  for (const auto& out_place : pn.post_set(out_transition_)) {
    const auto out_place_initial_count{pn.at_place(out_place).initial_marking_counts};

    // If the place is in the initial marking, this is passed to the output places of its output transition
    pn.set_initial_marking_token_count(out_place, place_initial_count + out_place_initial_count);

    // All outgoing arcs from the output transition are also added to the input transition
    pn.add_arc(in_transition_, out_place, 1);
  }

  // The place and its incoming/outgoing arcs are removed
  pn.erase_node(place_);
  // The output transition and its incoming/outgoing arcs are removed
  pn.erase_node(out_transition_);
}

std::optional<fusion_series_tau_and_keep_transition> fusion_series_tau_and_keep_transition::try_build(
    const petri_net_builder& pn, const std::unordered_set<node_id_type>& keep_transitions,
    const fusion_series_tau_and_keep_transition::node_id_type& node_id) {
  // The node is a place
  if (!pn.is_place(node_id)) {
    return std::nullopt;
  }
  const node_id_type& place{node_id};

  // The place is SESE with arc weights = 1
  const auto is_sese{get_if_single_entry_single_exit_with_weight(pn, place, 1)};
  if (!is_sese.has_value()) {
    return std::nullopt;
  }
  const auto& [in_transition, out_transition]{is_sese.value()};

  // The input and output transitions are distinct
  if (in_transition == out_transition) {
    return std::nullopt;
  }

  // The place's input transition is not "keep" and the node's output transition is "keep"
  if (keep_transitions.contains(in_transition) || !keep_transitions.contains(out_transition)) {
    return std::nullopt;
  }

  // The input transition has only one output place (the place)
  if (pn.post_set(in_transition) != std::unordered_set<node_id_type>{place}) {
    return std::nullopt;
  }

  // The place is not part of the initial marking
  //    (otherwise, it is easy to construct a net that would change its language after reduction)
  if (pn.is_initial_place(place)) {
    return std::nullopt;
  }

  // BECAUSE WE REPRESENT A MARKING AS BITSET:
  const auto in_transition_pre_set{pn.pre_set(in_transition)};

  // The pre-sets of the input and output transitions are disjoint
  const auto is_out_transition_pre_set_f{
      [pre_set = pn.pre_set(out_transition)](const auto& p) { return pre_set.contains(p); }};
  if (std::ranges::any_of(in_transition_pre_set, is_out_transition_pre_set_f)) {
    return std::nullopt;
  }
  // If the place is part of the final marking, then the input places of its input transition are not
  const auto is_final_place_f{[&pn = std::as_const(pn)](const auto& p) { return pn.is_final_place(p); }};
  if (pn.is_final_place(place) && std::ranges::any_of(in_transition_pre_set, is_final_place_f)) {
    return std::nullopt;
  }

  return fusion_series_tau_and_keep_transition{in_transition, place, out_transition};
}

void fusion_series_tau_and_keep_transition::apply(petri_net_builder& pn) const {
  // Final counts for the place
  auto place_final_count{pn.is_final_place(place_) ? pn.at_place(place_).final_marking_counts : 0};

  for (const auto& in_place : pn.pre_set(in_transition_)) {
    const auto in_place_final_count{pn.at_place(in_place).final_marking_counts};

    // If the place is in the final marking, this is passed to the input places of its input transition
    pn.set_final_marking_token_count(in_place, place_final_count + in_place_final_count);

    // All arcs leading to the input transition are added to the output transition
    pn.add_arc(in_place, out_transition_, 1);
  }

  // The place and its incoming/outgoing arcs are removed
  pn.erase_node(place_);
  // The input transition and its incoming/outgoing arcs are removed
  pn.erase_node(in_transition_);
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
