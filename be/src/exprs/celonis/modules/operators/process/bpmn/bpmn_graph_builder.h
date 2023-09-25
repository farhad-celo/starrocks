#pragma once

#include <initializer_list>
#include <vector>

#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/edge.h"
#include "modules/operators/process/bpmn/vertex.h"
#include "modules/operators/process/bpmn/vertex_types.h"

namespace celonis::accelerator {

struct edge_config final {
  operators::process::bpmn::vertex_id_type from_vertex_id;
  std::vector<operators::process::bpmn::vertex_id_type> to_vertex_ids;
};

class bpmn_graph_builder final {
 public:
  bpmn_graph_builder& vertices(const std::vector<operators::process::bpmn::vertex>& vertices);
  bpmn_graph_builder& vertex(operators::process::bpmn::vertex v);
  template <typename... ARGS>
  bpmn_graph_builder& vertex(ARGS&&... args) {
    vertices_.emplace_back(std::forward<ARGS>(args)...);
    return *this;
  }
  operators::process::bpmn::vertex_id_type add_vertex(operators::process::bpmn::vertex_type type);
  bpmn_graph_builder& edges(const std::vector<operators::process::bpmn::edge>& edges);
  bpmn_graph_builder& edges(std::initializer_list<edge_config> edge_configs_init_list);
  bpmn_graph_builder& edge(operators::process::bpmn::edge e);
  template <typename... ARGS>
  bpmn_graph_builder& edge(ARGS&&... args) {
    edges_.emplace_back(std::forward<ARGS>(args)...);
    return *this;
  }
  [[nodiscard]] size_t next_vertex_index() const noexcept { return vertices_.size(); };
  [[nodiscard]] operators::process::bpmn::bpmn_graph build() const;

  const std::vector<operators::process::bpmn::edge>& get_edges() { return edges_; };
  const std::vector<operators::process::bpmn::vertex>& get_vertices() { return vertices_; };

 private:
  std::vector<operators::process::bpmn::vertex> vertices_{};
  std::vector<operators::process::bpmn::edge> edges_{};
};

}  // namespace celonis::accelerator
