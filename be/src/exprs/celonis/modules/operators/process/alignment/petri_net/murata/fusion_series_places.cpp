#include "modules/operators/process/alignment/petri_net/murata/fusion_series_places.h"

#include <optional>
#include <unordered_set>

#include <boost/graph/adjacency_list.hpp>

#include "modules/operators/process/alignment/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

namespace {

bool is_single_entry_single_exit_node(const petri_net_builder& pn,
                                      const petri_net_builder_node::node_id_type& node_id) {
  if (!pn.is_node(node_id)) {
    return false;
  }
  const auto& vertex{pn.get_vertex(node_id)};
  return boost::out_degree(vertex, pn.graph()) == 1 && boost::in_degree(vertex, pn.graph()) == 1;
}

bool is_single_entry_single_exit_with_weight(const petri_net_builder& pn,
                                             const petri_net_builder_node::node_id_type& node_id,
                                             petri_net_builder::arc_weight_type weight) {
  if (!is_single_entry_single_exit_node(pn, node_id)) {
    return false;
  }
  const auto& vertex{pn.get_vertex(node_id)};
  const auto in_edge_it{boost::in_edges(vertex, pn.graph()).first};
  const auto out_edge_it{boost::out_edges(vertex, pn.graph()).first};
  const auto& in_weight{pn.graph()[*in_edge_it]};
  const auto& out_weight{pn.graph()[*out_edge_it]};

  return in_weight == weight && out_weight == weight;
};

}  // namespace

std::optional<fusion_of_series_places> fusion_of_series_places::try_build(
    const petri_net_builder& pn, const std::unordered_set<node_id_type>& keep_transitions,
    const node_id_type& node_id) {
  // Node is a transition
  // The transition is not "keep"
  // Node is SESE with the same arc weights (= 1)
  if (!pn.is_transition(node_id) || keep_transitions.contains(node_id) ||
      !is_single_entry_single_exit_with_weight(pn, node_id, 1)) {
    return std::nullopt;
  }
  const node_id_type& transition{node_id};

  // NOLINTNEXTLINE(readability-qualified-auto,-warnings-as-errors)
  const auto vertex{pn.get_vertex(transition)};
  const auto in_edge_desc{*boost::in_edges(vertex, pn.graph()).first};
  // NOLINTNEXTLINE(readability-qualified-auto,-warnings-as-errors)
  const auto in_place_desc{boost::source(in_edge_desc, pn.graph())};
  // Input place has only one outgoing transition
  if (boost::out_degree(in_place_desc, pn.graph()) != 1) {
    return std::nullopt;
  }
  const auto& in_node{pn.graph()[in_place_desc]};
  const auto& in_place{in_node.get_place()};

  // Incoming and outgoing places are distinct
  // Incoming/outgoing arcs have the same weight
  // Input and output places are distinct
  const auto out_edge_desc{*boost::out_edges(vertex, pn.graph()).first};
  // NOLINTNEXTLINE(readability-qualified-auto,-warnings-as-errors)
  const auto out_place_desc{boost::target(out_edge_desc, pn.graph())};
  if (in_place_desc == out_place_desc || pn.graph()[in_edge_desc] != pn.graph()[out_edge_desc]) {
    return std::nullopt;
  }
  const auto& out_node{pn.graph()[out_place_desc]};
  const auto& out_place{out_node.get_place()};

  // BECAUSE WE REPRESENT A MARKING AS BITSET:
  // The "redirect arc" does not exist (this is only because we represent a marking as a bitset)
  for (const auto in_in_edge_desc : boost::make_iterator_range(boost::in_edges(in_place_desc, pn.graph()))) {
    // NOLINTNEXTLINE(readability-qualified-auto,-warnings-as-errors)
    if (const auto src_transition_desc{boost::source(in_in_edge_desc, pn.graph())};
        boost::edge(src_transition_desc, out_place_desc, pn.graph()).second) {
      return std::nullopt;
    }
  }

  // The input and output places are not simultaneously initial/final places
  if ((in_place.is_initial() && out_place.is_initial()) || (in_place.is_final() && out_place.is_final())) {
    return std::nullopt;
  }

  fusion_of_series_places ret{in_node.id(), transition, out_node.id()};
  return ret;
};

void fusion_of_series_places::apply(petri_net_builder& pn) const {
  // The transition and its incoming/outgoing arcs are removed
  pn.erase_node(transition_);
  const auto& in_place{pn.at_place(in_place_)};
  const auto& out_place{pn.at_place(out_place_)};

  // If the input place is in the initial/final marking, this is passed to the output place
  if (pn.is_initial_place(in_place_)) {
    pn.set_initial_marking_token_count(out_place_, in_place.initial_marking_counts + out_place.initial_marking_counts);
  }
  if (pn.is_final_place(in_place_)) {
    pn.set_final_marking_token_count(out_place_, in_place.final_marking_counts + out_place.final_marking_counts);
  }

  // All arcs leading to the input place are also added to the output place
  for (const auto& in_edge_desc : boost::make_iterator_range(boost::in_edges(pn.get_vertex(in_place_), pn.graph()))) {
    const auto& in_transition{pn.graph()[boost::source(in_edge_desc, pn.graph())].id()};
    debug_assert(in_place_ != out_place_);
    // It is fine to directly modify the net here because in_place_ and out_place_ are distinct
    pn.add_arc(in_transition, out_place_, 1);
  }

  // The input place and its incoming/outgoing arcs are removed
  pn.erase_node(in_place_);
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
