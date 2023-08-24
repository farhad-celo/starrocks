#include "cut_strategy.h"

#include "ctl/assert.h"

namespace celonis::accelerator::operators::process::sub_dfgs {

void accumulate_non_empty_traces_per_dfg(std::vector<directly_follows_graph>& sub_dfgs) {
  for (auto& dfg : sub_dfgs) {
    const auto& start_vertices{dfg[boost::graph_bundle].start_vertices};
    const auto& end_vertices{dfg[boost::graph_bundle].end_vertices};

    constexpr auto accumulate{[](const auto& vertices) {
      return std::accumulate(vertices.begin(), vertices.end(), size_t{0}, [](auto acc, const auto& vertex) {
        const auto [key, value]{vertex};
        return acc + value;
      });
    }};

    dfg[boost::graph_bundle].log.trace_count = accumulate(start_vertices);
    debug_assert(dfg[boost::graph_bundle].log.trace_count == accumulate(end_vertices));
  }
}

void add_start_and_end_vertices_from_old_dfg(const directly_follows_graph& old_dfg,
                                             const std::vector<size_t>& vertex_to_component, sub_dfgs_t& sub_graphs) {
  std::vector<directly_follows_graph>& sub_dfgs{sub_graphs.sub_dfgs};
  const std::vector<size_t>& old_vertex_to_new_vertex{sub_graphs.old_vertex_to_new_vertex};

  for (const auto& [start_vertex, start_count] : old_dfg[boost::graph_bundle].start_vertices) {
    const auto component{vertex_to_component[start_vertex]};

    // Node ids change when building the smaller sub DFGs, therefore we need to use old_vertex_to_new_vertex
    const auto new_node_id{old_vertex_to_new_vertex[start_vertex]};
    sub_dfgs[component][boost::graph_bundle].start_vertices[new_node_id] += start_count;
  }

  for (const auto& [end_vertex, end_count] : old_dfg[boost::graph_bundle].end_vertices) {
    const auto component{vertex_to_component[end_vertex]};
    sub_dfgs[component][boost::graph_bundle].end_vertices[old_vertex_to_new_vertex[end_vertex]] += end_count;
  }
}

sub_dfgs_t build_sub_dfgs_from_components(const directly_follows_graph& old_dfg, const cut_t& cut,
                                          const bool add_counts_for_inter_component_edges) {
  const auto& [component_count, vertex_to_component]{cut};
  std::vector<directly_follows_graph> sub_dfgs(component_count);
  std::vector<size_t> old_vertex_to_new_vertex(boost::num_vertices(old_dfg));

  // Iterate over old vertices and insert them in the corresponding sub DFG
  for (const auto& v : boost::make_iterator_range(boost::vertices(old_dfg))) {
    const auto component{vertex_to_component[v]};
    const auto vertex_id{boost::add_vertex(old_dfg[v], sub_dfgs[component])};
    old_vertex_to_new_vertex[v] = vertex_id;
  }

  // Iterate over edges of old_dfg and insert them into the corresponding sub DFGs
  for (const auto& edge : boost::make_iterator_range(boost::edges(old_dfg))) {
    // Edges still refer to old node_ids, need to remap to node_ids of sub DFGs
    const auto& source{boost::source(edge, old_dfg)};
    const auto& target{boost::target(edge, old_dfg)};
    const auto source_component{vertex_to_component[source]};
    const auto target_component{vertex_to_component[target]};

    if (source_component == target_component) {
      boost::add_edge(old_vertex_to_new_vertex[source], old_vertex_to_new_vertex[target], old_dfg[edge],
                      sub_dfgs[source_component]);
    } else if (add_counts_for_inter_component_edges) {
      // Edges between components are cut and we make the source and target vertices end / start vertices
      sub_dfgs[source_component][boost::graph_bundle].end_vertices[old_vertex_to_new_vertex[source]] +=
          old_dfg[edge].count;
      sub_dfgs[target_component][boost::graph_bundle].start_vertices[old_vertex_to_new_vertex[target]] +=
          old_dfg[edge].count;
    }
  }
  return sub_dfgs_t(sub_dfgs, old_vertex_to_new_vertex);
}

}  // namespace celonis::accelerator::operators::process::sub_dfgs
