#pragma once

#include <unordered_map>
#include <vector>

#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/bpmn_graph_builder.h"
#include "modules/operators/process/bpmn/bpmn_graph_with_block_structure_types.h"
#include "modules/operators/process/bpmn/edge.h"
#include "modules/operators/process/bpmn/vertex_types.h"
#include "modules/operators/process/inductive_miner/process_tree.h"

namespace celonis::accelerator::operators::process::bpmn {

class bpmn_graph_with_block_structure final : public bpmn_graph {
 public:
  // multimap-like for shared events which can have the same vertex ID but different blocks assigned.
  using vertex_id_to_block_id_mapping_t = std::map<vertex_id_type, bpmn_block_ids_t>;
  bpmn_graph_with_block_structure() = default;
  bpmn_graph_with_block_structure(bpmn_graph graph, bpmn_blocks_t blocks,
                                  vertex_id_to_block_id_mapping_t vertex_id_to_block_id_mapping);
  bpmn_graph_with_block_structure(bpmn_graph graph, bpmn_blocks_t blocks,
                                  const block_id_to_contained_vertices_mapping_t& block_to_node_mapping);

  [[nodiscard]] const bpmn_graph& graph() const;
  [[nodiscard]] const bpmn_blocks_t& blocks() const;
  [[nodiscard]] const vertex_id_to_block_id_mapping_t& vertex_id_to_block_id_mapping() const;
  [[nodiscard]] bpmn_blocks_t blocks_for_vertex_id(vertex_id_type vertex_id) const;

 private:
  bpmn_blocks_t blocks_;
  vertex_id_to_block_id_mapping_t vertex_id_to_block_id_mapping_;
};

class bpmn_graph_with_block_structure_builder final : public bpmn_graph_builder {
 public:
  bpmn_graph_with_block_structure_builder() = default;
  bpmn_graph_with_block_structure_builder& block(bpmn_block value);
  bpmn_graph_with_block_structure_builder& blocks(const bpmn_blocks_t& values);
  bpmn_block_id_t add_block(bpmn_block_id_t parent_id, bpmn_block_object_id_t oid, bpmn_block_type type);
  [[nodiscard]] bpmn_block_id_t add_root_block(bpmn_block_object_id_t oid);
  // NB: Assume the block already exists and was previously added via 'block(...)'
  bpmn_graph_with_block_structure_builder& add_vertex_id_to_block_id_mapping(vertex_id_type vertex_id,
                                                                             bpmn_block_id_t block_id);
  bpmn_graph_with_block_structure_builder& mappings(
      const bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping_t& vertex_id_to_block_id_mapping);
  bpmn_graph_with_block_structure_builder& mappings(
      const block_id_to_contained_vertices_mapping_t& block_to_node_mapping);
  [[nodiscard]] vertex_id_type add_vertex_to_current_block(vertex_type type);
  [[nodiscard]] bpmn_graph_with_block_structure build_with_block_structure() const;

 private:
  [[nodiscard]] bpmn_block_id_t next_block_index() const;
  bpmn_blocks_t blocks_;
  block_id_to_contained_vertices_mapping_t block_to_node_mapping_;
};

}  // namespace celonis::accelerator::operators::process::bpmn
