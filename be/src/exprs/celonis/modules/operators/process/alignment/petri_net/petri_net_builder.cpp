#include "modules/operators/process/alignment/petri_net/petri_net_builder.h"

#include <ranges>

#include "legacy_embedded_ctl/conversion.h"
#include "modules/common/exceptions.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

petri_net_builder::petri_net_builder(const petri_net_representation& pn_repr) {
  common::runtime_assert(std::ranges::all_of(pn_repr.initial_marking,
                                             [&pn_repr](const auto& node) { return pn_repr.places.contains(node); }),
                         "petri_net_representation object's initial_marking has an invalid node.");
  common::runtime_assert(std::ranges::all_of(pn_repr.final_marking,
                                             [&pn_repr](const auto& node) { return pn_repr.places.contains(node); }),
                         "petri_net_representation object's final_marking has an invalid node.");

  for (const auto& place_id : pn_repr.places) {
    const auto initial_marking_count{legacy_embedded_ctl::cast<marking_counter_type>(pn_repr.initial_marking.count(place_id))};
    const auto final_marking_count{legacy_embedded_ctl::cast<marking_counter_type>(pn_repr.final_marking.count(place_id))};
    add_place(place_id, {initial_marking_count, final_marking_count});
  }
  for (const auto& [transition_id, label] : pn_repr.transitions) {
    add_transition(transition_id, {label});
  }
  for (const auto& [src_place_id, tgt_transition_id] : pn_repr.place_transition_arcs) {
    add_arc(src_place_id, tgt_transition_id, 1);
  }
  for (const auto& [src_transition_id, tgt_place_id] : pn_repr.transition_place_arcs) {
    add_arc(src_transition_id, tgt_place_id, 1);
  }
}

petri_net_representation petri_net_builder::to_petri_net_representation() {
  petri_net_representation pn_repr{};
  for (const auto& vertex_desc : boost::make_iterator_range(boost::vertices(graph_))) {
    const auto& vertex{boost::get(boost::vertex_bundle, graph_, vertex_desc)};
    if (vertex.is_place()) {
      const auto& place{vertex.get_place()};
      const auto& place_id{vertex.id()};

      pn_repr.places.emplace(place_id);
      if (place.is_initial()) {
        common::runtime_assert(place.initial_marking_counts == 1,
                               "petri_net_representation requires safe (i.e. token counts <= 1) initial markings");
        pn_repr.initial_marking.emplace(place_id);
      }
      if (place.is_final()) {
        common::runtime_assert(place.final_marking_counts == 1,
                               "petri_net_representation requires safe (i.e. token counts <= 1) final markings.");
        pn_repr.final_marking.emplace(place_id);
      }
    } else if (vertex.is_transition()) {
      const auto& transition{vertex.get_transition()};
      const auto& transition_id{vertex.id()};
      pn_repr.transitions.emplace(transition_id, transition.label);
    } else {
      legacy_embedded_ctl::assert_unreachable();
    }
  }

  for (const auto& edge_desc : boost::make_iterator_range(boost::edges(graph_))) {
    const auto& src_node_desc{boost::source(edge_desc, graph_)};
    const auto& tgt_node_desc{boost::target(edge_desc, graph_)};
    const auto& src_node{boost::get(boost::vertex_bundle, graph_, src_node_desc)};
    const auto& tgt_node{boost::get(boost::vertex_bundle, graph_, tgt_node_desc)};

    const auto edge_weight{graph_[edge_desc]};
    // For now we do not support weighted graphs
    legacy_embedded_debug_assert(edge_weight == 1);
    if (src_node.is_place()) {
      legacy_embedded_debug_assert(tgt_node.is_transition());
      for (petri_net_builder::arc_weight_type i{0}; i < edge_weight; ++i) {
        pn_repr.place_transition_arcs.emplace(src_node.id(), tgt_node.id());
      }
    } else {
      legacy_embedded_debug_assert(tgt_node.is_place());
      for (petri_net_builder::arc_weight_type i{0}; i < edge_weight; ++i) {
        pn_repr.transition_place_arcs.emplace(src_node.id(), tgt_node.id());
      }
    }
  }

  return pn_repr;
}

void petri_net_builder::add_place(const node_id_type& node_id, petri_net_builder_node::place_data place) {
  common::runtime_assert(!is_node(node_id), "Trying to add place for already existing node ID [{}]", node_id);
  // NOLINTNEXTLINE(readability-qualified-auto,-warnings-as-errors)
  const auto vertex_desc{boost::add_vertex({node_id, place}, graph_)};
  id_map_.emplace(node_id, vertex_desc);
}

void petri_net_builder::add_transition(const node_id_type& node_id,
                                       petri_net_builder_node::transition_data transition) {
  common::runtime_assert(!is_node(node_id), "Trying to add transition for already existing node ID [{}]", node_id);
  // NOLINTNEXTLINE(readability-qualified-auto,-warnings-as-errors)
  const auto vertex_desc{boost::add_vertex({node_id, transition}, graph_)};
  id_map_.emplace(node_id, vertex_desc);
}

void petri_net_builder::add_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id,
                                arc_weight_type weight) {
  common::runtime_assert(is_node(src_node_id), "Trying to add arc [({}, {})] for non-existing source node [{}]",
                         src_node_id, tgt_node_id, src_node_id);
  common::runtime_assert(is_node(tgt_node_id), "Trying to add arc [({}, {})] for non-existing target node [{}]",
                         src_node_id, tgt_node_id, tgt_node_id);
  common::runtime_assert(
      (is_place(src_node_id) && is_transition(tgt_node_id)) || (is_transition(src_node_id) && is_place(tgt_node_id)),
      "Arc [({}, {})] (to be added) is not bipartite", src_node_id, tgt_node_id);
  const auto [edge, _]{add_edge_by_label(src_node_id, tgt_node_id, arc_weight_type{0})};
  graph_[edge] += weight;
}

void petri_net_builder::erase_node(const node_id_type& node_id) {
  common::runtime_assert(is_node(node_id), "Trying to erase a non-existing node [{}]", node_id);
  const auto vertex_desc{id_map_.at(node_id)};  // NOLINT(readability-qualified-auto,-warnings-as-errors)
  boost::clear_vertex(vertex_desc, graph_);
  boost::remove_vertex(vertex_desc, graph_);
  id_map_.erase(node_id);
}

void petri_net_builder::erase_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id) {
  common::runtime_assert(is_arc(src_node_id, tgt_node_id), "Trying to erase a non-existing arc [({}, {})]", src_node_id,
                         tgt_node_id);
  remove_edge_by_label(src_node_id, tgt_node_id);
}

void petri_net_builder::set_initial_marking_token_count(const node_id_type& node_id, marking_counter_type count) {
  common::runtime_assert(is_place(node_id), "Trying to set initial token count for non-existing place [{}]", node_id);
  auto place{at_place(node_id)};
  place.initial_marking_counts = count;
  graph_[get_vertex(node_id)].set_data(place);
}

void petri_net_builder::set_final_marking_token_count(const node_id_type& node_id, marking_counter_type count) {
  common::runtime_assert(is_place(node_id), "Trying to set final token count for non-existing place [{}]", node_id);
  auto place{at_place(node_id)};
  place.final_marking_counts = count;
  graph_[get_vertex(node_id)].set_data(place);
}

bool petri_net_builder::is_node(const node_id_type& node_id) const {
  return get_vertex(node_id) != boost::graph_traits<graph_type>::null_vertex();
}

bool petri_net_builder::is_place(const node_id_type& node_id) const {
  return is_node(node_id) && at_node(node_id).is_place();
}

bool petri_net_builder::is_transition(const node_id_type& node_id) const {
  return is_node(node_id) && at_node(node_id).is_transition();
}

bool petri_net_builder::is_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id) const {
  return is_place_transition_arc(src_node_id, tgt_node_id) || is_transition_place_arc(src_node_id, tgt_node_id);
}

bool petri_net_builder::is_place_transition_arc(const node_id_type& src_node_id,
                                                const node_id_type& tgt_node_id) const {
  return is_place(src_node_id) && is_transition(tgt_node_id) && edge_by_label(src_node_id, tgt_node_id).second;
}

bool petri_net_builder::is_transition_place_arc(const node_id_type& src_node_id,
                                                const node_id_type& tgt_node_id) const {
  return is_transition(src_node_id) && is_place(tgt_node_id) && edge_by_label(src_node_id, tgt_node_id).second;
}

bool petri_net_builder::is_initial_place(const node_id_type& node_id) const {
  return is_place(node_id) && at_place(node_id).is_initial();
}

bool petri_net_builder::is_final_place(const node_id_type& node_id) const {
  return is_place(node_id) && at_place(node_id).is_final();
}

size_t petri_net_builder::in_degree(const node_id_type& node_id) const {
  common::runtime_assert(is_node(node_id), "Checking In-Degree for non-existing node [{}]", node_id);
  const auto& vertex_desc{get_vertex(node_id)};
  return boost::in_degree(vertex_desc, graph_);
}

size_t petri_net_builder::out_degree(const node_id_type& node_id) const {
  common::runtime_assert(is_node(node_id), "Checking Out-Degree for non-existing node [{}]", node_id);
  const auto& vertex_desc{get_vertex(node_id)};
  return boost::out_degree(vertex_desc, graph_);
}

const petri_net_builder_node& petri_net_builder::at_node(const node_id_type& node_id) const {
  common::runtime_assert(is_node(node_id), "Trying to access non-existing node [{}].", node_id);
  return graph_[get_vertex(node_id)];
}

const petri_net_builder_node::place_data& petri_net_builder::at_place(const node_id_type& node_id) const {
  common::runtime_assert(is_place(node_id), "Trying to access non-existing place [{}].", node_id);
  return at_node(node_id).get_place();
}

const petri_net_builder_node::transition_data& petri_net_builder::at_transition(const node_id_type& node_id) const {
  common::runtime_assert(is_transition(node_id), "Trying to access non-existing transition [{}].", node_id);
  return at_node(node_id).get_transition();
}

petri_net_builder::arc_weight_type petri_net_builder::at_arc(const node_id_type& src_node_id,
                                                             const node_id_type& tgt_node_id) const {
  common::runtime_assert(is_arc(src_node_id, tgt_node_id), "Trying to retrieve non-existing arc [({}, {})]",
                         src_node_id, tgt_node_id);
  const auto [edge, _]{edge_by_label(src_node_id, tgt_node_id)};
  return graph_[edge];
}

boost::iterator_range<petri_net_builder::id_map_type::const_iterator> petri_net_builder::get_nodes() const {
  return boost::make_iterator_range(std::cbegin(id_map_), std::cend(id_map_));
}

size_t petri_net_builder::node_count() const { return boost::num_vertices(graph_); }

std::unordered_set<petri_net_builder::node_id_type> petri_net_builder::pre_set(const node_id_type& node_id) const {
  common::runtime_assert(is_node(node_id), "Can't compute pre-set for non-existing node [{}]", node_id);
  std::unordered_set<node_id_type> ret{};

  const auto& vertex_desc{get_vertex(node_id)};
  for (const auto& in_arc : boost::make_iterator_range(boost::in_edges(vertex_desc, graph_))) {
    ret.insert(graph_[boost::source(in_arc, graph_)].id());
  }

  return ret;
}

std::unordered_set<petri_net_builder::node_id_type> petri_net_builder::post_set(const node_id_type& node_id) const {
  common::runtime_assert(is_node(node_id), "Can't compute post-set for non-existing node [{}]", node_id);
  std::unordered_set<node_id_type> ret{};

  const auto& vertex_desc{get_vertex(node_id)};
  for (const auto& out_arc : boost::make_iterator_range(boost::out_edges(vertex_desc, graph_))) {
    ret.insert(graph_[boost::target(out_arc, graph_)].id());
  }

  return ret;
}

petri_net_builder::vertex_descriptor petri_net_builder::get_vertex(const node_id_type& node_id) const {
  if (const auto it{id_map_.find(node_id)}; it != std::cend(id_map_)) {
    return it->second;
  }
  return boost::graph_traits<graph_type>::null_vertex();
}

std::pair<petri_net_builder::edge_descriptor, bool> petri_net_builder::edge_by_label(
    const node_id_type& src_node_id, const node_id_type& tgt_node_id) const {
  return boost::edge(get_vertex(src_node_id), get_vertex(tgt_node_id), graph_);
}

std::pair<petri_net_builder::edge_descriptor, bool> petri_net_builder::add_edge_by_label(
    const node_id_type& src_node_id, const node_id_type& tgt_node_id, const arc_weight_type& weight) {
  return boost::add_edge(get_vertex(src_node_id), get_vertex(tgt_node_id), weight, graph_);
}

void petri_net_builder::remove_edge_by_label(const node_id_type& src_node_id, const node_id_type& tgt_node_id) {
  boost::remove_edge(get_vertex(src_node_id), get_vertex(tgt_node_id), graph_);
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
