#pragma once

#include <map>
#include <optional>
#include <variant>
#include <vector>

#include "legacy_embedded_ctl/static_array.h"
#include "modules/operators/process/bpmn/edge.h"
#include "modules/operators/process/bpmn/vertex.h"
#include "modules/operators/process/inductive_miner/process_tree.h"

namespace celonis::accelerator {
class BpmnModelDescription;
}

namespace celonis::accelerator::operators::process::bpmn {

/**
 * BPMN graph, representing a graph structure consisting of start and end vertices, as well as tasks, exclusive choice
 * and parallel gateway.
 */
class bpmn_graph {
 public:
  using edge_collection = std::vector<edge>;       // Edges without any specific order
  using vertex_ids = std::vector<vertex_id_type>;  // Vertex ids without any specific order

  // TODO(bluppes): investigate performance impact of keeping this map ordered
  using vertex_map = std::map<vertex_id_type, vertex>;
  using adjacent_vertex_map =
      std::unordered_map<vertex_id_type,
                         vertex_ids>;  // Mapping from a vertex id to all adjacent vertices identified by their id
  using adjacent_edge_map =
      std::unordered_map<vertex_id_type,
                         edge_collection>;  // Mapping from a vertex id to all adjacent edges identified by their id
  using vertex_to_activity_ptr_t = std::unordered_map<vertex_id_type, row_id>;  // Mapping from a (task) vertex to it's
                                                                                // corresponding activity pointer

  bpmn_graph() = default;

  bpmn_graph(std::initializer_list<vertex> vertices, std::initializer_list<edge> edges);
  bpmn_graph(const std::vector<vertex>& vertices, edge_collection edges);

  [[nodiscard]] const vertex_map& get_vertices() const noexcept { return vertices_; }
  [[nodiscard]] const edge_collection& get_edges() const noexcept { return edges_; }
  [[nodiscard]] const vertex& get_vertex(vertex_id_type id) const;

  [[nodiscard]] const adjacent_edge_map& get_outgoing_edges() const;
  [[nodiscard]] const edge_collection& get_outgoing_edges(const vertex_id_type vertex) const {
    return get_outgoing_edges().at(vertex);
  }
  [[nodiscard]] const edge_collection& get_outgoing_edges(const vertex v) const {
    return get_outgoing_edges(v.get_vertex_id());
  }

  [[nodiscard]] const adjacent_edge_map& get_ingoing_edges() const;
  [[nodiscard]] const edge_collection& get_ingoing_edges(const vertex_id_type vertex) const {
    return get_ingoing_edges().at(vertex);
  }
  [[nodiscard]] const edge_collection& get_ingoing_edges(const vertex v) const {
    return get_ingoing_edges(v.get_vertex_id());
  };

  [[nodiscard]] const vertex_to_activity_ptr_t& get_vertex_to_activity_mapping() const;
  [[nodiscard]] const std::unordered_map<row_id, vertex_id_type>& activity_id_to_vertex_id() const;

  [[nodiscard]] const vertex_ids& start_vertex_ids() const;
  [[nodiscard]] const vertex_ids& end_vertex_ids() const;
  // There are some algorithms (most notably replay) that only work with single start and end node. These functions are
  // helpers for such code.
  [[nodiscard]] vertex_id_type single_start_vertex() const noexcept;
  [[nodiscard]] vertex_id_type single_end_vertex() const noexcept;
  [[nodiscard]] bool is_single_object() const;

  // TODO(j.kruska) Here for compatibility with code that used the adjacency graph, decide if both ingoing edges and
  // vertices are needed
  [[nodiscard]] const adjacent_vertex_map& outgoing_vertices() const;
  [[nodiscard]] const adjacent_vertex_map& ingoing_vertices() const;

 private:
  vertex_map vertices_{};
  edge_collection edges_{};
  struct caches {
    std::optional<adjacent_vertex_map> ingoing_vertices;
    std::optional<adjacent_vertex_map> outgoing_vertices;
    std::optional<std::unordered_map<row_id, vertex_id_type>> activity_id_to_vertex_id;
    std::optional<vertex_to_activity_ptr_t> vertex_to_activity_mapping;
    std::optional<adjacent_edge_map> ingoing_edges;
    std::optional<adjacent_edge_map> outgoing_edges;
    std::optional<vertex_ids> start_vertices{};
    std::optional<vertex_ids> end_vertices{};
    std::optional<bool> is_single_object{};
  };
  mutable caches caches_{};
};

/**
 * Extracts the single object BPMN subgraph of a given object id from a multi object BPMN graph. Does not leave isolated
 * vertices, i.e. the resulting graph only contains tasks and gateways that are actually used in the flow of the object
 * in question.
 *
 * @param graph A multi object BPMN graph.
 * @param object ID of the object in question.
 * @return The single object subgraph
 */
[[nodiscard]] bpmn_graph extract_single_object_bpmn_graph(const bpmn_graph& graph, object_id object);

/**
 * Maps each different vertex type to an int value.
 */
[[nodiscard]] cel_int_t convert_vertex_type_to_int(const vertex_type& type);
[[nodiscard]] vertex_type convert_int_to_vertex_type(cel_int_t type_int);

/**
 * Creates a bpmn_graph from a protobuf message
 * @return The BPMN model.
 */
[[nodiscard]] bpmn_graph convert_from_proto(const BpmnModelDescription& bpmn_proto,
                                            const memory::column_t& activity_column,
                                            common::execution_context& operator_context);

using bpmn_to_string_t = std::unordered_map<vertex_id_type, std::string>;
/**
 * Creates a bpmn_graph from the protobuf message, and creates strings for all vertices.
 *
 * In particular, note that the bpmn_graph only stores row ids into the activity column for its tasks.
 * Names of tasks that are not in the activity column can thus not be retrieved from the bpmn_graph alone.
 * On top of these task names, this also generates names for gateways that are consistent with the BPMN input.
 *
 * @return The BPMN model and a map from vertex ids to string identifiers
 */
std::pair<bpmn_graph, bpmn_to_string_t> convert_from_proto_and_create_string_map(
    const BpmnModelDescription& bpmn_proto, const memory::column_t& activity_column,
    common::execution_context& operator_context);

[[nodiscard]] std::string to_string(const bpmn_graph& model);

}  // namespace celonis::accelerator::operators::process::bpmn
