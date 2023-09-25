#include "modules/operators/process/bpmn/bpmn_graph_builder.h"

namespace celonis::accelerator {

bpmn_graph_builder& bpmn_graph_builder::vertices(const std::vector<operators::process::bpmn::vertex>& vertices) {
  vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
  return *this;
}

bpmn_graph_builder& bpmn_graph_builder::vertex(operators::process::bpmn::vertex v) {
  vertices_.emplace_back(v);
  return *this;
}

operators::process::bpmn::vertex_id_type bpmn_graph_builder::add_vertex(operators::process::bpmn::vertex_type type) {
  auto id{next_vertex_index()};
  vertex(id, type);
  return id;
}

bpmn_graph_builder& bpmn_graph_builder::edges(const std::vector<operators::process::bpmn::edge>& edges) {
  edges_.insert(edges_.end(), edges.begin(), edges.end());
  return *this;
}

bpmn_graph_builder& bpmn_graph_builder::edges(std::initializer_list<edge_config> edge_configs_init_list) {
  std::vector<operators::process::bpmn::edge> edges{};
  for (const auto& [from_id, to_ids] : edge_configs_init_list) {
    for (const auto to_id : to_ids) {
      edges.emplace_back(from_id, to_id);
    }
  }
  return this->edges(edges);
}

bpmn_graph_builder& bpmn_graph_builder::edge(operators::process::bpmn::edge e) {
  edges_.push_back(e);
  return *this;
}

operators::process::bpmn::bpmn_graph bpmn_graph_builder::build() const {
  return operators::process::bpmn::bpmn_graph{vertices_, edges_};
}

}  // namespace celonis::accelerator
