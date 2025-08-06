#include "modules/operators/process/petri_net/murata/fusion_series_places.h"

#include <optional>
#include <unordered_set>

#include "modules/operators/process/petri_net/murata/reduction_rules_utils.h"
#include "modules/operators/process/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

std::optional<fusion_series_places> fusion_series_places::try_build(
    const petri_net_builder& pn, const std::unordered_set<node_id_type>& keep_transitions,
    const node_id_type& node_id) {
  // Node is a transition
  // The transition is not "keep"
  if (!pn.is_transition(node_id) || keep_transitions.contains(node_id)) {
    return std::nullopt;
  }
  const node_id_type& transition{node_id};

  // Node is SESE with the same arc weights (= 1)
  const auto is_sese{get_if_single_entry_single_exit_with_weight(pn, transition, 1)};
  if (!is_sese.has_value()) {
    return std::nullopt;
  }
  const auto& [in_place, out_place]{is_sese.value()};

  // Input and output places are distinct
  if (in_place == out_place) {
    return std::nullopt;
  }

  // Input place has only one outgoing transition
  if (pn.out_degree(in_place) != 1) {
    return std::nullopt;
  }

  // BECAUSE WE REPRESENT A MARKING AS BITSET:
  // The "redirect arc" does not exist
  for (const auto& in_in_transition : pn.pre_set(in_place)) {
    if (pn.is_arc(in_in_transition, out_place)) {
      return std::nullopt;
    }
  }

  // The input and output places are not simultaneously initial/final places
  if ((pn.is_initial_place(in_place) && pn.is_initial_place(out_place)) ||
      (pn.is_final_place(in_place) && pn.is_final_place(out_place))) {
    return std::nullopt;
  }

  return fusion_series_places{in_place, transition, out_place};
};

void fusion_series_places::apply(petri_net_builder& pn) const {
  // The transition and its incoming/outgoing arcs are removed
  pn.erase_node(transition_);
  const auto in_place{pn.at_place(in_place_)};
  const auto out_place{pn.at_place(out_place_)};

  // If the input place is in the initial/final marking, this is passed to the output place
  if (pn.is_initial_place(in_place_)) {
    pn.set_initial_marking_token_count(out_place_, in_place.initial_marking_counts + out_place.initial_marking_counts);
  }
  if (pn.is_final_place(in_place_)) {
    pn.set_final_marking_token_count(out_place_, in_place.final_marking_counts + out_place.final_marking_counts);
  }

  // All arcs leading to the input place are also added to the output place
  for (const auto& in_transition : pn.pre_set(in_place_)) {
    pn.add_arc(in_transition, out_place_, 1);
  }

  // The input place and its incoming/outgoing arcs are removed
  pn.erase_node(in_place_);
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
