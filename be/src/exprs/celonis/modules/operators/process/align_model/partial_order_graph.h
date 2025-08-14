#pragma once

#include <optional>

#include <boost/graph/adjacency_list.hpp>

#include "align_model_types.h"

namespace celonis::accelerator::operators::process::align_model {

struct partial_order_vertex_properties {
  // Multiple vertices in this graph may refer to the same bpmn vertex. Optional is empty if it's an unmapped move.
  // This means that for unmapped moves we "lose" the information of the label corresponding to that
  std::optional<cpml::model::bpmn::vertex_id_type> bpmn_vertex_id{std::nullopt};
  alignment_move_type move_type{alignment_move_type::UNMAPPED_MOVE};  // do we need to know the move type?
  bool operator==(const partial_order_vertex_properties& rhs) const = default;
};

struct partial_order_edge_properties {
  edge_type type{edge_type::UNMAPPED};
};

// Note: When changing this, please beware that we use the fact that node iterators are random-access in several places.
using partial_order_graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
                                                  partial_order_vertex_properties, partial_order_edge_properties>;

using partial_edge_iter = boost::graph_traits<partial_order_graph>::edge_iterator;
using partial_in_edge_iter = boost::graph_traits<partial_order_graph>::in_edge_iterator;

using partial_vertex_t = boost::graph_traits<partial_order_graph>::vertex_descriptor;
using partial_edge_t = boost::graph_traits<partial_order_graph>::edge_descriptor;

}  // namespace celonis::accelerator::operators::process::align_model
