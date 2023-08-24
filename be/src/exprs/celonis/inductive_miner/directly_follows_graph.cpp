#include "directly_follows_graph.h"

#include <algorithm>
#include <numeric>
#include <utility>

#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/strong_components.hpp>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for_each.h>

#include "ctl/conversion.h"
#ifdef CELOSTAR
#include "inductive_miner/splittable_eventlog.h"
#else
#include "ctl/static_array.h"
#include "ctl/utility.h"
#endif
#include "modules/common/case_aligned_range.h"
#include "modules/common/execution_context.h"
#include "modules/common/for_each_group.h"
#ifndef CELOSTAR
#include "modules/cube/filter_bitset.h"
#include "modules/memory/column.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog.h"
#endif

namespace celonis::accelerator::operators::process {

dfg_pre_aggregation reduce_pre_aggregates(std::vector<dfg_pre_aggregation>&& dfg_pre_aggs) {
  dfg_pre_aggregation return_pre_agg{0};
  if (dfg_pre_aggs.empty()) {
    return return_pre_agg;
  }

  auto first_pre_agg_it{std::begin(dfg_pre_aggs)};
  return std::accumulate(std::next(first_pre_agg_it), std::end(dfg_pre_aggs), std::move(*first_pre_agg_it),
                         [](auto acc_pre_agg, auto cur_pre_agg) {
                           acc_pre_agg.log_properties.trace_count += cur_pre_agg.log_properties.trace_count;
                           acc_pre_agg.log_properties.contains_empty_trace +=
                               cur_pre_agg.log_properties.contains_empty_trace;

                           auto& acc_activity_statistics{acc_pre_agg.activity_statistics};
                           auto& cur_activity_statistics{cur_pre_agg.activity_statistics};
                           debug_assert(cur_activity_statistics.size() == acc_activity_statistics.size());
                           std::transform(std::cbegin(cur_activity_statistics), std::cend(cur_activity_statistics),
                                          std::cbegin(acc_activity_statistics), std::begin(acc_activity_statistics),
                                          [](const auto& acc_counters, const auto& cur_counters) {
                                            return dfg_pre_aggregation::activity_counters{
                                                acc_counters.frequency_count + cur_counters.frequency_count,
                                                acc_counters.start_count + cur_counters.start_count,
                                                acc_counters.end_count + cur_counters.end_count};
                                          });

                           for (const auto& [key, value] : cur_pre_agg.edge_statistics) {
                             acc_pre_agg.edge_statistics[key] += value;
                           }
                           return acc_pre_agg;
                         });
}

namespace dfg {
namespace {

auto vertices_not_in_a_path(directly_follows_graph& dfg) {
  static constexpr row_id source_id{-1};
  static constexpr row_id target_id{-2};
  const auto source_descriptor(boost::add_vertex({source_id}, dfg));
  const auto sink_descriptor(boost::add_vertex({target_id}, dfg));

  for (const auto& start_vertex : dfg[boost::graph_bundle].start_vertices) {
    boost::add_edge(source_descriptor, start_vertex.first, dfg);
  }
  for (const auto& end_vertex : dfg[boost::graph_bundle].end_vertices) {
    boost::add_edge(end_vertex.first, sink_descriptor, dfg);
  }

  using vertices_size_type = boost::graph_traits<directly_follows_graph>::vertices_size_type;
  std::vector<vertices_size_type> distances_from_start(boost::num_vertices(dfg), 0);
  std::vector<vertices_size_type> distances_from_end(boost::num_vertices(dfg), 0);

  auto from_start_visitor{boost::visitor(boost::make_bfs_visitor(boost::record_distances(
      boost::make_iterator_property_map(distances_from_start.begin(), boost::get(boost::vertex_index, dfg)),
      boost::on_tree_edge())))};
  boost::breadth_first_search(dfg, source_descriptor, from_start_visitor);

  const auto reverse_dfg{boost::make_reverse_graph(dfg)};
  auto from_end_visitor{boost::visitor(boost::make_bfs_visitor(boost::record_distances(
      boost::make_iterator_property_map(distances_from_end.begin(), boost::get(boost::vertex_index, dfg)),
      boost::on_tree_edge())))};
  boost::breadth_first_search(reverse_dfg, sink_descriptor, from_end_visitor);

  boost::clear_vertex(sink_descriptor, dfg);
  boost::clear_vertex(source_descriptor, dfg);
  boost::remove_vertex(sink_descriptor, dfg);
  boost::remove_vertex(source_descriptor, dfg);

  distances_from_start.resize(boost::num_vertices(dfg));
  distances_from_end.resize(boost::num_vertices(dfg));

  return std::make_pair(distances_from_start, distances_from_end);
}

/**
 * Fixes the filtered DFG graph to ensure the DFG invariant:
 *    all vertices are reachable from a start node and all vertices can reach an end node.
 * If a vertex X is not reachable from a start node, we add all of its incoming Y->X edges in the original DFG
 *  where Y is not on the same strongly connected component as X in the filtered DFG.
 * Analogously, if a vertex X cannot reach an end vertex, we add all of its outgoing X->Y edges in the original DFG
 *  where Y is not on the same strongly connected component as X in the filtered DFG.
 *
 * @param dfg the original DFG
 * @param filtered_dfg the filtered DFG
 */
void fix_filtered_graph(const directly_follows_graph& dfg, directly_follows_graph& filtered_dfg) {
  static_assert(std::is_same_v<size_t, boost::graph_traits<directly_follows_graph>::vertex_descriptor>);

  auto [distances_from_start, distances_to_end]{vertices_not_in_a_path(filtered_dfg)};

  auto num_vertices{boost::num_vertices(filtered_dfg)};
  std::vector<size_t> component_mapping(num_vertices);
  [[maybe_unused]] const auto strong_component_count{boost::strong_components(filtered_dfg, component_mapping.data())};

  for (size_t target_vertex_index{0}; target_vertex_index < distances_from_start.size(); ++target_vertex_index) {
    if (distances_from_start[target_vertex_index] == 0) {
      const auto& target_vertex{boost::vertex(target_vertex_index, dfg)};
      for (const auto& edge : boost::make_iterator_range(boost::in_edges(target_vertex, dfg))) {
        // This returns a vertex descriptor which is an int because the underlying data-structure is a vector
        const auto& source_vertex{boost::source(edge, dfg)};
        if (component_mapping[target_vertex_index] != component_mapping[source_vertex]) {
          boost::add_edge(source_vertex, target_vertex, filtered_dfg);
        }
      }
    }
  }

  for (size_t source_vertex_index{0}; source_vertex_index < distances_to_end.size(); ++source_vertex_index) {
    if (distances_to_end[source_vertex_index] == 0) {
      const auto& source_vertex{boost::vertex(source_vertex_index, dfg)};
      for (const auto& edge : boost::make_iterator_range(boost::out_edges(source_vertex, dfg))) {
        const auto& target_vertex{boost::target(edge, dfg)};
        if (component_mapping[source_vertex_index] != component_mapping[target_vertex]) {
          boost::add_edge(source_vertex, target_vertex, filtered_dfg);
        }
      }
    }
  }
}

}  // namespace

directly_follows_graph build_dfg(dfg_pre_aggregation&& dfg_pre_agg) {
  directly_follows_graph dfg{};

#ifdef CELOSTAR
  size_t activity_vertex_mapping[dfg_pre_agg.activity_statistics.size()];
#else
  auto activity_vertex_mapping{ctl::make_static_array_value_init<size_t>(dfg_pre_agg.activity_statistics.size(),
                                                                         ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};
#endif

  for (row_id id{0}; id < ctl::cast<row_id>(dfg_pre_agg.activity_statistics.size()); ++id) {
    const auto& activity_statistics{dfg_pre_agg.activity_statistics[id]};

    if (activity_statistics.frequency_count == 0) {
      continue;
    }
    const auto vertex_id{boost::add_vertex({id}, dfg)};
    activity_vertex_mapping[id] = vertex_id;

    dfg[vertex_id].count += activity_statistics.frequency_count;
    if (activity_statistics.start_count > 0) {
      dfg[boost::graph_bundle].start_vertices[vertex_id] += activity_statistics.start_count;
    }
    if (activity_statistics.end_count > 0) {
      dfg[boost::graph_bundle].end_vertices[vertex_id] += activity_statistics.end_count;
    }
  }

  for (const auto& [edge, count] : dfg_pre_agg.edge_statistics) {
    const auto& [from_activity, to_activity]{edge};
    const auto from_vertex{activity_vertex_mapping[from_activity]};
    const auto to_vertex{activity_vertex_mapping[to_activity]};
    add_edge(from_vertex, to_vertex, count, dfg);
  }

  std::swap(dfg[boost::graph_bundle].log, dfg_pre_agg.log_properties);

  return dfg;
}

void add_edge(size_t from, size_t to, size_t multiplicity, directly_follows_graph& dfg) {
  const auto& descriptor_and_success{boost::add_edge(from, to, dfg)};
  dfg[descriptor_and_success.first].count += multiplicity;
}

#ifdef CELOSTAR
void filter_dfg_count_map(phmap::flat_hash_map<size_t, size_t>& map, const dfg_filter_config& filter_config) {
#else
void filter_dfg_count_map(ska::bytell_hash_map<size_t, size_t>& map, const dfg_filter_config& filter_config) {
#endif
  // Find maximum cardinality.
  size_t max_count{0};
  for (const auto& pair : map) {
    max_count = std::max(max_count, pair.second);
  }

  // Remove objects.
  const auto threshold_count =
      static_cast<size_t>(static_cast<cel_float_t>(max_count) * filter_config.vertices_threshold);
  auto it{map.begin()};
  while (it != map.end()) {
    // Remember: `threshold_count` has already been rounded towards zero.
    if (it->second <= threshold_count) {
      it = map.erase(it);
    } else {
      ++it;
    }
  }
}

directly_follows_graph filter_dfg_edges(const directly_follows_graph& dfg, const dfg_filter_config& filter_config) {
  using edge_descriptor = boost::graph_traits<directly_follows_graph>::edge_descriptor;
  using vertex_descriptor = boost::graph_traits<directly_follows_graph>::vertex_descriptor;

  auto filtered_dfg{dfg};
  // If we remove an edge, its target vertex might become unreachable.
  // In that case, re-add its most frequent predecessor later.
  // (The alternative considered was to remove only edges that do not leave
  // their targets unreachable. However, this behavior would heavily depend
  // on the order the vertices are visited in.)
  std::unordered_map<vertex_descriptor, std::pair<vertex_descriptor, dfg_edge_properties>>
      most_frequent_deleted_predecessors;
  // Avoid rehashing! Remember: start vertices will never become "unreachable".
  most_frequent_deleted_predecessors.reserve(boost::num_vertices(filtered_dfg) -
                                             filtered_dfg[boost::graph_bundle].start_vertices.size());

  const auto vertices{boost::make_iterator_range(boost::vertices(filtered_dfg))};
  for (const auto& v : vertices) {
    size_t max_count{0};

    // TODO (goulart.e): Although this is necessary to pass all tests, should it be controllable via PQL?
    if (filter_config.edges_consider_end_activities) {
      const auto& end_vertices{filtered_dfg[boost::graph_bundle].end_vertices};
      const auto& it{end_vertices.find(v)};
      if (it != end_vertices.end()) {
        max_count = it->second;
      }
    }

    // Find maximum cardinality.
    const auto edges{boost::make_iterator_range(boost::out_edges(v, filtered_dfg))};
    for (const auto& e : edges) {
      max_count = std::max(max_count, filtered_dfg[e].count);
    }

    // Remove edges.
    const auto threshold_count =
        static_cast<size_t>(static_cast<cel_float_t>(max_count) * filter_config.edges_threshold);
    const auto predicate = [threshold_count, &filtered_dfg, &most_frequent_deleted_predecessors](edge_descriptor e) {
      // Remember: `threshold_count` has already been rounded towards zero.
      const auto& count{filtered_dfg[e].count};
      // If we remove this edge, store its most frequent predecessor to be able to re-add it later.
      if (count <= threshold_count) {
        const auto& source{boost::source(e, filtered_dfg)};
        const auto& target{boost::target(e, filtered_dfg)};
        const auto& start_vertices{filtered_dfg[boost::graph_bundle].start_vertices};
        if (start_vertices.find(target) == start_vertices.end()) {
          auto& [source_descriptor, edge_properties]{most_frequent_deleted_predecessors[target]};
          if (count > edge_properties.count) {
            source_descriptor = source;
            edge_properties = filtered_dfg[e];
          }
        }
        return true;
      }
      return false;
    };
    boost::remove_out_edge_if(v, predicate, filtered_dfg);
  }

  // If a vertex became unreachable during the removal of edges, re-add its most frequent predecessor.
  for (const auto& [target_descriptor, source_and_edge] : most_frequent_deleted_predecessors) {
    const auto& [source_descriptor, edge_properties]{source_and_edge};
    const auto in_edges{boost::in_edges(target_descriptor, filtered_dfg)};
    if (in_edges.first == in_edges.second) {
      boost::add_edge(source_descriptor, target_descriptor, edge_properties, filtered_dfg);
    }
  }

  fix_filtered_graph(dfg, filtered_dfg);
  return filtered_dfg;
}

directly_follows_graph initialize_dfg(const splittable_eventlog& eventlog,
                                      const common::execution_context& parent_context, size_t grain_size) {
  const auto context{parent_context.create_sub_context("initialize_dfg", {})};
  tbb::enumerable_thread_specific<dfg_pre_aggregation> dfg_pre_aggs{eventlog.activity_domain_count()};
#ifndef CELOSTAR
  std::visit(
      [grain_size, &dfg_pre_aggs](auto eventlog_data) {
        common::for_each_group(
            element<PICK_TRACE_ID>(eventlog_data), grain_size,
            [&eventlog_data](auto interval, auto& local_pre_agg) {
              if (std::empty(interval)) {
                return;
              }
              ++local_pre_agg.activity_statistics[eventlog_data[std::begin(interval)].activity_id()].start_count;
              ++local_pre_agg.activity_statistics[eventlog_data[std::end(interval) - 1].activity_id()].end_count;
              ++local_pre_agg.log_properties.trace_count;
              // update the first activity's count here, and all others as we add edges
              ++local_pre_agg.activity_statistics[eventlog_data[std::begin(interval)].activity_id()].frequency_count;
              for (auto source_index{std::begin(interval)}, target_index{source_index + 1};
                   target_index != std::end(interval); ++source_index, ++target_index) {
                const auto source_id{eventlog_data[source_index].activity_id()};
                const auto target_id{eventlog_data[target_index].activity_id()};
                ++local_pre_agg.activity_statistics[target_id].frequency_count;
                local_pre_agg.add_edge(static_cast<row_id>(source_id), static_cast<row_id>(target_id), 1);
              }
            },
            dfg_pre_aggs);
      },
      eventlog.current_split_eventlog_view());
#else
 std::visit(
     [grain_size, &dfg_pre_aggs, &eventlog](auto eventlog_data) {
       common::for_each_group(
           element<PICK_TRACE_ID>(eventlog_data), grain_size,
           [&eventlog_data, &eventlog](auto interval, auto& local_pre_agg) {
             if (std::empty(interval)) {
               return;
             }
             size_t multiplicity = eventlog.get_variant_multiplicity(eventlog_data[std::begin(interval)].trace_id());
             local_pre_agg.activity_statistics[eventlog_data[std::begin(interval)].activity_id()].start_count += multiplicity;
             local_pre_agg.activity_statistics[eventlog_data[std::end(interval) - 1].activity_id()].end_count += multiplicity;
             local_pre_agg.log_properties.trace_count += multiplicity;
             // update the first activity's count here, and all others as we add edges
             local_pre_agg.activity_statistics[eventlog_data[std::begin(interval)].activity_id()].frequency_count += multiplicity;
             for (auto source_index{std::begin(interval)}, target_index{source_index + 1};
                  target_index != std::end(interval); ++source_index, ++target_index) {
               const auto source_id{eventlog_data[source_index].activity_id()};
               const auto target_id{eventlog_data[target_index].activity_id()};
               local_pre_agg.activity_statistics[target_id].frequency_count += multiplicity;
               local_pre_agg.add_edge(static_cast<row_id>(source_id), static_cast<row_id>(target_id), multiplicity);
             }
           },
           dfg_pre_aggs);
     },
     eventlog.current_split_eventlog_view());
#endif
  std::vector dfg_pre_agg_vec(std::move_iterator(dfg_pre_aggs.begin()), std::move_iterator(dfg_pre_aggs.end()));
  auto dfg_pre_agg{reduce_pre_aggregates(std::move(dfg_pre_agg_vec))};
  return dfg::build_dfg(std::move(dfg_pre_agg));
}

}  // namespace dfg
}  // namespace celonis::accelerator::operators::process
