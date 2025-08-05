#include "bpmn_remap_task_ids.h"

#include <algorithm>
#include <unordered_map>

#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/bpmn_graph_builder.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

/** Mapping from old to new vertex IDs */
using vertex_id_mapping_t = std::unordered_map<vertex_id_type, vertex_id_type>;

vertex_id_mapping_t remap_vertices(const bpmn_graph& graph, const legacy_embedded_ctl::static_array<row_id>& mapping_vector,
                                   bpmn_graph_builder& bldr) {
  vertex_id_mapping_t new_vertex_ids{};
  std::ranges::for_each(graph.get_vertices(), [&](const auto& pair) {
    const auto& vertex_type{pair.second.get_vertex_type()};
    if (std::holds_alternative<bpmn::task>(vertex_type)) {
      row_id new_task_id{mapping_vector.at(std::get<bpmn::task>(vertex_type).activity_id)};
      const auto vertex_id{bldr.add_vertex(bpmn::task{new_task_id})};
      new_vertex_ids[pair.second.get_vertex_id()] = vertex_id;
    } else {
      const auto vertex_id{bldr.add_vertex(vertex_type)};
      new_vertex_ids[pair.second.get_vertex_id()] = vertex_id;
    }
  });
  return new_vertex_ids;
}

void remap_edges(const bpmn_graph& graph, const vertex_id_mapping_t& new_vertex_ids, bpmn_graph_builder& bldr) {
  std::ranges::for_each(graph.get_edges(), [&](const auto& p) {
    vertex_id_type new_source_id{new_vertex_ids.at(p.get_source_id())};
    vertex_id_type new_target_id{new_vertex_ids.at(p.get_target_id())};
    bldr.edge(new_source_id, new_target_id, p.get_object_id(), p.get_count());
  });
}

void remap_node_id_to_block_id_mapping(const bpmn_graph_with_block_structure& graph,
                                       const vertex_id_mapping_t& new_vertex_ids,
                                       bpmn_graph_with_block_structure_builder& bldr) {
  std::ranges::for_each(
      graph.vertex_id_to_block_id_mapping(), [&bldr, &new_vertex_ids](const auto& vertex_id_to_block_ids) {
        const auto [old_vertex_id, block_ids]{vertex_id_to_block_ids};
        std::ranges::for_each(block_ids, [&, old_vid = old_vertex_id](const bpmn_block_id_t block_id) {
          bldr.add_vertex_id_to_block_id_mapping(new_vertex_ids.at(old_vid), block_id);
        });
      });
}

}  // anonymous namespace

bpmn_graph remap_task_ids(const bpmn_graph& graph, const legacy_embedded_ctl::static_array<row_id>& mapping_vector) {
  bpmn_graph_builder bldr{};
  const auto new_vertex_ids{remap_vertices(graph, mapping_vector, bldr)};
  remap_edges(graph, new_vertex_ids, bldr);
  return bldr.build();
}

bpmn_graph_with_block_structure remap_task_ids(const bpmn_graph_with_block_structure& graph,
                                               const legacy_embedded_ctl::static_array<row_id>& mapping_vector) {
  bpmn_graph_with_block_structure_builder bldr{};
  const auto new_vertex_ids{remap_vertices(graph, mapping_vector, bldr)};
  remap_edges(graph, new_vertex_ids, bldr);

  // Block structure specific logic:
  // 1.) Copy over blocks (unmodified), and
  std::ranges::for_each(graph.blocks(), [&bldr](const auto& block) { bldr.block(block); });
  // 2.) add vertex to block mapping (with new vertex IDs)
  remap_node_id_to_block_id_mapping(graph, new_vertex_ids, bldr);

  return bldr.build_with_block_structure();
}

}  // namespace celonis::accelerator::operators::process::bpmn
