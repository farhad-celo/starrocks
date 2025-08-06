#pragma once

#include <optional>
#include <type_traits>
#include <unordered_set>
#include <variant>

#include <boost/graph/adjacency_list.hpp>
#include <bytell_hash_map.hpp>

#include "modules/operators/process/petri_net/petri_net.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

using marking_counter_type = int32_t;

class petri_net_builder_node {
 public:
  struct place_data {
    marking_counter_type initial_marking_counts;
    marking_counter_type final_marking_counts;

    [[nodiscard]] bool is_initial() const { return initial_marking_counts != 0; }
    [[nodiscard]] bool is_final() const { return final_marking_counts != 0; }
  };
  struct transition_data {
    using label_type = string_to_int_mapper::label_id_t;
    label_type label{};
  };
  using node_id_type = std::string;
  using node_data_type = std::variant<place_data, transition_data>;

  // Default constructor otherwise boost::adjacency_list complains
  petri_net_builder_node() = default;
  petri_net_builder_node(node_id_type id, place_data place) : id_{std::move(id)}, data_{place} {}
  petri_net_builder_node(node_id_type id, transition_data transition) : id_{std::move(id)}, data_{transition} {}

  [[nodiscard]] bool is_place() const { return std::holds_alternative<place_data>(data_); }
  [[nodiscard]] bool is_transition() const { return std::holds_alternative<transition_data>(data_); }

  [[nodiscard]] const node_id_type& id() const { return id_; }

  [[nodiscard]] const place_data& get_place() const {
    common::runtime_assert(is_place(), "Trying to extract a place from node that is not a place.");
    return std::get<place_data>(data_);
  }
  [[nodiscard]] const transition_data& get_transition() const {
    common::runtime_assert(is_transition(), "Trying to extract a transition from node that is not a transition.");
    return std::get<transition_data>(data_);
  }
  void set_data(node_data_type data) { data_ = data; }

 private:
  // If it's not trivially copyable, must change its usage to move and add methods to return non-const references
  static_assert(std::is_trivially_copyable_v<place_data>);
  static_assert(std::is_trivially_copyable_v<transition_data>);
  static_assert(std::is_trivially_copyable_v<node_data_type>);

  node_id_type id_{};
  node_data_type data_{};
};

/**
 * This is a builder class for a system net (i.e. a Petri net plus one initial marking and one final marking).
 * The Petri net can have arbitrary arc weights and token counts. The class is not optimized for performance, so it
 * might be too slow if you have to repetitively manipulate your Petri net.
 *
 * It enforces constraints that node IDs are unique, that one (possibly invisible) label is assigned for each
 * transition, and that the graph is bipartite. The first two constraints are enforced by the structure of its
 * underlying graph data-structure, while the last is enforced by correct implementation of the modifier functions.
 *
 * TODO (goulart.e) this should eventually replace the petri_net_representation class
 * TODO (goulart.e) the API for this builder should be typed
 */
class petri_net_builder {
 public:
  using arc_weight_type = int64_t;

  // We need to efficiently retrieve and remove vertices, so hash_setS is chosen for the vertex list. Additionally, we
  //  want to enforce that this is not a multi-graph, so setS is also chosen for the edge lists (hash_setS seems to
  //  have a bug). This causes some extra memory overhead compared to vecS and some overhead when iterating over
  //  incoming/outgoing edges compared to vecS/listS. Also, the graph is bidirectional since we also want to get the
  //  incoming arcs of a node.
  // Using hash_setS as the outgoing edges list breaks the code. It seems to be a bug of boost
  using graph_type = boost::adjacency_list<boost::hash_setS, boost::setS, boost::bidirectionalS, petri_net_builder_node,
                                           arc_weight_type>;
  // First attempt was to use boost::labeled_graph, but its "remove_vertex" method is broken
  //  see: https://github.com/boostorg/graph/issues/167
  // boost::named_graph might also be an alternative but its documentation is very bad
  using node_id_type = petri_net_builder_node::node_id_type;
  using vertex_descriptor = boost::graph_traits<graph_type>::vertex_descriptor;
  using edge_descriptor = boost::graph_traits<graph_type>::edge_descriptor;
  using id_map_type = ska::bytell_hash_map<node_id_type, vertex_descriptor>;

  petri_net_builder() = default;
  [[nodiscard]] explicit petri_net_builder(const petri_net_representation& pn_repr);
  [[nodiscard]] petri_net_representation to_petri_net_representation();

  // MODIFIERS
  void add_place(const node_id_type& id, petri_net_builder_node::place_data place);
  void add_transition(const node_id_type& id, petri_net_builder_node::transition_data transition);
  /** Adds arc from src to tgt nodes. Both nodes are required to exist. If the arc already exists, increment it */
  void add_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id, arc_weight_type weight);
  /** Removes the node and all of its incoming/outgoing arcs */
  void erase_node(const node_id_type& node_id);
  void erase_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id);
  void set_initial_marking_token_count(const node_id_type& node_id, marking_counter_type count);
  void set_final_marking_token_count(const node_id_type& node_id, marking_counter_type count);

  // QUERYING
  [[nodiscard]] bool is_node(const node_id_type& node_id) const;
  [[nodiscard]] bool is_place(const node_id_type& node_id) const;
  [[nodiscard]] bool is_transition(const node_id_type& node_id) const;
  [[nodiscard]] bool is_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id) const;
  [[nodiscard]] bool is_place_transition_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id) const;
  [[nodiscard]] bool is_transition_place_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id) const;
  [[nodiscard]] bool is_initial_place(const node_id_type& node_id) const;
  [[nodiscard]] bool is_final_place(const node_id_type& node_id) const;
  // ALSO QUERYING but returns a number. This functions will throw with the node is not on the graph
  [[nodiscard]] size_t in_degree(const node_id_type& node_id) const;
  [[nodiscard]] size_t out_degree(const node_id_type& node_id) const;

  // GETTERS They are checked and will throw on a bad access
  [[nodiscard]] const petri_net_builder_node& at_node(const node_id_type& node_id) const;
  [[nodiscard]] const petri_net_builder_node::place_data& at_place(const node_id_type& node_id) const;
  [[nodiscard]] const petri_net_builder_node::transition_data& at_transition(const node_id_type& node_id) const;
  [[nodiscard]] arc_weight_type at_arc(const node_id_type& src_node_id, const node_id_type& tgt_node_id) const;
  // GRAPH GETTERS they don't throw but you can't modify the graph in parallel or it will break
  // Not happy with this since it leaks implementation details for this class
  [[nodiscard]] boost::iterator_range<id_map_type::const_iterator> get_nodes() const;
  [[nodiscard]] size_t node_count() const;

  // COMPUTE STUFF
  [[nodiscard]] std::unordered_set<node_id_type> pre_set(const node_id_type& node_id) const;
  [[nodiscard]] std::unordered_set<node_id_type> post_set(const node_id_type& node_id) const;

 protected:
  // Methods that we must implement ourselves because boost::labeled_graph is broken.
  // We use a similar API to the boost::labeled_graph since that our hope is that in the future we can reuse it
  [[nodiscard]] vertex_descriptor get_vertex(const node_id_type& node_id) const;

  [[nodiscard]] std::pair<edge_descriptor, bool> edge_by_label(const node_id_type& src_node_id,
                                                               const node_id_type& tgt_node_id) const;
  [[nodiscard]] std::pair<edge_descriptor, bool> add_edge_by_label(const node_id_type& src_node_id,
                                                                   const node_id_type& tgt_node_id,
                                                                   const arc_weight_type& weight);
  void remove_edge_by_label(const node_id_type& src_node_id, const node_id_type& tgt_node_id);

  graph_type graph_{};    // NOLINT(misc-non-private-member-variables-in-classes)
  id_map_type id_map_{};  // NOLINT(misc-non-private-member-variables-in-classes)
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
