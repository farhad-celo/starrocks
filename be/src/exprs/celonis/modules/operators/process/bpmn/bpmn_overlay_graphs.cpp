#include "bpmn_overlay_graphs.h"

#include <algorithm>
#include <unordered_map>
#include <variant>

#include "legacy_embedded_ctl/utility.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/bpmn_graph_builder.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

using activity_id_to_vertex_mapping_t = std::unordered_map<row_id, vertex>;

[[nodiscard]] activity_id_to_vertex_mapping_t create_activity_id_to_vertex_mapping(
    const bpmn_graph::vertex_map& vertices) {
  activity_id_to_vertex_mapping_t activity_id_to_vertex_mapping{};
  for (const auto& vertex : vertices) {
    std::visit(legacy_embedded_ctl::overloaded{[](const auto& /*vertex_type*/) {},
                               [&activity_id_to_vertex_mapping, &vertex](const task& task) {
                                 activity_id_to_vertex_mapping.insert({task.activity_id, vertex.second});
                               }},
               vertex.second.get_vertex_type());
  }
  return activity_id_to_vertex_mapping;
}

/** Constructs a bpmn_graph_builder which represents the state of the given graph */
[[nodiscard]] bpmn_graph_builder builder_from_graph(const bpmn_graph& graph) {
  bpmn_graph_builder bldr{};
  bldr.edges(graph.get_edges());
  // TODO(j.kruska) This can lead to collisions
  std::ranges::for_each(graph.get_vertices(), [&bldr](const auto& pair) { bldr.vertex(pair.second); });
  return bldr;
}

/** Same as above for block structured graph */
[[nodiscard]] bpmn_graph_with_block_structure_builder builder_from_graph(const bpmn_graph_with_block_structure& graph) {
  bpmn_graph_with_block_structure_builder bldr{};
  bldr.edges(graph.get_edges());
  std::ranges::for_each(graph.get_vertices(), [&bldr](const auto& pair) { bldr.vertex(pair.second); });
  // Block structure specific logic:
  // 1.) Copy over blocks (unmodified), and
  std::ranges::for_each(graph.blocks(), [&bldr](const auto& block) { bldr.block(block); });
  // 2.) add vertex to block mapping
  std::ranges::for_each(graph.vertex_id_to_block_id_mapping(), [&bldr](const auto& vertex_id_to_block_ids) {
    const auto [vertex_id, block_ids]{vertex_id_to_block_ids};
    std::ranges::for_each(block_ids, [&bldr, vid = vertex_id](const bpmn_block_id_t block_id) {
      bldr.add_vertex_id_to_block_id_mapping(vid, block_id);
    });
  });
  return bldr;
}

/** Mapping from old to new vertex IDs */
using vertex_id_mapping_t = std::unordered_map<vertex_id_type, vertex_id_type>;

[[nodiscard]] vertex_id_mapping_t remap_vertices(const bpmn_graph::vertex_map& vertices,
                                                 const activity_id_to_vertex_mapping_t& activity_id_to_vertex_mapping,
                                                 bpmn_graph_builder& bldr) {
  vertex_id_mapping_t index_mapping{};
  std::ranges::transform(vertices, std::inserter(index_mapping, std::end(index_mapping)),
                         [&bldr, &activity_id_to_vertex_mapping](const auto& vertex) {
                           return std::visit(
                               legacy_embedded_ctl::overloaded{[&bldr, &vertex](const auto& v) {
                                                 const auto vertex_id{bldr.add_vertex(v)};
                                                 return std::make_pair(vertex.second.get_vertex_id(), vertex_id);
                                               },
                                               [&bldr, &vertex, &activity_id_to_vertex_mapping](const task& task) {
                                                 auto lhs_task = activity_id_to_vertex_mapping.find(task.activity_id);
                                                 if (lhs_task == activity_id_to_vertex_mapping.end()) {
                                                   const auto vertex_id{bldr.add_vertex({task})};
                                                   return std::make_pair(vertex.second.get_vertex_id(), vertex_id);
                                                 }
                                                 return std::make_pair(vertex.second.get_vertex_id(),
                                                                       lhs_task->second.get_vertex_id());
                                               }},
                               vertex.second.get_vertex_type());
                         });
  return index_mapping;
}

/** Mapping from old to new vertex IDs */
using block_id_mapping_t = std::unordered_map<bpmn_block_id_t, bpmn_block_id_t>;

/** Block IDs of two or more bpmn graphs can overlap. Therefore, we remap them here such that each block ID is unique */
[[nodiscard]] block_id_mapping_t remap_blocks(const bpmn_blocks_t& blocks, const object_id oid,
                                              bpmn_graph_with_block_structure_builder& bldr) {
  block_id_mapping_t block_id_mapping{{NO_BLOCK_PARENT_ID, NO_BLOCK_PARENT_ID}};
  std::ranges::transform(blocks, std::inserter(block_id_mapping, std::end(block_id_mapping)),
                         [&bldr, oid, &block_id_mapping = std::as_const(block_id_mapping)](const bpmn_block& value) {
                           const auto new_parent_block_id{block_id_mapping.at(value.parent_id)};
                           const auto new_block_id{bldr.add_block(new_parent_block_id, oid, value.block_type)};
                           return std::make_pair(value.block_id, new_block_id);
                         });
  return block_id_mapping;
}

void remap_nodes_to_blocks_mapping(const bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping_t& old_mapping,
                                   const vertex_id_mapping_t& vertex_id_mapping,
                                   const block_id_mapping_t& block_id_mapping,
                                   bpmn_graph_with_block_structure_builder& bldr) {
  std::set<vertex_id_type> vids{};
  std::ranges::for_each(old_mapping, [&](const auto& vertex_id_to_block_ids) {
    const auto [old_vertex_id, old_block_ids]{vertex_id_to_block_ids};
    const auto new_vertex_id{vertex_id_mapping.at(old_vertex_id)};
    std::ranges::for_each(old_block_ids, [&](const bpmn_block_id_t old_block_id) {
      const auto new_block_id{block_id_mapping.at(old_block_id)};
      bldr.add_vertex_id_to_block_id_mapping(new_vertex_id, new_block_id);
    });
  });
}

[[nodiscard]] bpmn_graph_with_block_structure overlay(const bpmn_graph_with_block_structure& lhs,
                                                      const bpmn_graph_with_block_structure& rhs,
                                                      object_id rhs_object_id) {
  const auto lhs_activity_id_to_vertex_mapping{create_activity_id_to_vertex_mapping(lhs.get_vertices())};
  auto bldr{builder_from_graph(lhs)};
  const auto vertex_id_mapping(remap_vertices(rhs.get_vertices(), lhs_activity_id_to_vertex_mapping, bldr));
  for (const auto& e : rhs.get_edges()) {
    bldr.edge(edge{vertex_id_mapping.at(e.get_source_id()), vertex_id_mapping.at(e.get_target_id()), rhs_object_id,
                   e.get_count()});
  }

  // Block structure specific logic:

  // 1.) Remap blocks so that each block from each graph has a unique ID and the correct object ID assigned
  const auto block_id_mapping{remap_blocks(rhs.blocks(), rhs_object_id, bldr)};
  // 1.) Adjust the vertex ID to block ID mapping with the new mapping for vertices and blocks
  remap_nodes_to_blocks_mapping(rhs.vertex_id_to_block_id_mapping(), vertex_id_mapping, block_id_mapping, bldr);

  return bldr.build_with_block_structure();
}

}  // anonymous namespace

bpmn_graph overlay(const bpmn_graph& lhs, const bpmn_graph& rhs, const object_id rhs_object_id) {
  const auto lhs_tasks_mapped_to_activity_ids{create_activity_id_to_vertex_mapping(lhs.get_vertices())};
  auto bldr{builder_from_graph(lhs)};
  const auto index_mapping(remap_vertices(rhs.get_vertices(), lhs_tasks_mapped_to_activity_ids, bldr));

  for (const auto& e : rhs.get_edges()) {
    bldr.edge(
        edge{index_mapping.at(e.get_source_id()), index_mapping.at(e.get_target_id()), rhs_object_id, e.get_count()});
  }
  return bldr.build();
}

bpmn_graph_with_block_structure overlay(const std::vector<bpmn_graph_with_block_structure>& graphs,
                                        object_id initial_object_id) {
  return std::accumulate(graphs.begin(), graphs.end(), bpmn_graph_with_block_structure{},
                         [&initial_object_id](const bpmn_graph_with_block_structure& to_be_overlaid,
                                              const bpmn_graph_with_block_structure& to_overlay) {
                           return overlay(to_be_overlaid, to_overlay, initial_object_id++);
                         });
}

}  // namespace celonis::accelerator::operators::process::bpmn
