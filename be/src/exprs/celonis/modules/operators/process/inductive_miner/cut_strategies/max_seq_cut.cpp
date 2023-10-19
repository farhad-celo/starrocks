#include <algorithm>
#include <limits>
#include <unordered_map>

#include <boost/graph/connected_components.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#ifdef CELOSTAR
#include "ctl/assert.h"
#endif
#include "modules/operators/process/inductive_miner/cut_strategy.h"

namespace celonis::accelerator::operators::process {

namespace {

struct equivalence_relation {
  explicit equivalence_relation(size_t num_elements) : ids_(num_elements) {
    std::iota(begin(ids_), end(ids_), size_t{0});
  }
  void add(size_t i, size_t j) {
    if (i == j) {
      return;
    }
    const auto [min, max]{std::minmax(ids_.at(i), ids_.at(j))};
    std::ranges::replace(ids_, max, min);
  }
  [[nodiscard]] std::span<const int64_t> mapping() const { return std::span<const int64_t>{ids_}; }

 private:
  std::vector<int64_t> ids_{};
};

/**
 * This function iterates over the dfg components which were discovered by the find sequence cut method and finds
 * components which can be skipped. Consecutive skippable components are then merged. The resulting tree is less
 * complicated and more precise as shown in the example below. Traces: {"A", "B", "C"},{"A"} Before:
 * seq(A,xor(tau,B),xor(tau,C)) After: seq(xor(A,xor(tau,seq(B,C)))
 *
 *  Details about the function implemented below can be found in:
 *    - ProM Reference implementation: class CutFinderIMSequenceStrict, class CutFinderIMSequence
 *    - PhD Thesis of Sander Leemans: 5.6.4 Optionality under Sequence and keyword SequenceCutStrict (especially p. 233)
 *
 *    Log = [<A, B, C, D, E, F>, <A, B, E, F>]
 *
 *    This is the component mapping returned by the sequence cut find function
 *
 *    vertex_component_mapping = [0,1,2,3,4,5]
 *
 *    The algorithm uses three vectors to determine which components should be merged: max_to, min_from,
 * has_skipping_edges. For the running example they are initialized as follows:
 *      - max_to = [1,4,3,4,5,MAX_COMP]       # ID of max target component of edges leaving this component
 *      - min_from = [MIN_COMP,0,1,2,1,4]     # ID of min input component of edges entering this component
 *      - has_skipping_edges = [0,0,1,1,0,0]  # True for component "j" if there exists an edge X_i -> X_k with i < j < k
 *
 *    In a dfg with no skippable components, we would expect to see consecutive ids in max_to and min_from because
 *    we are only traversing from on component to its neighbouring component. However, as in our example we have
 *    components which are skipped, this is not the case. In our example, if we look into max_to, we see that when we
 *    go from component 1 to 2, the max_to value goes from 4 to 3, this is called an inversion. Formally, an inversion
 *    is a sequence of consecutive components X_{i-1}, X_i with max_to/min_from[X_{i-1}] < max_to/min_from[X_i].
 *
 *    The example code contains the following inversion:
 *      - (inv_start=2, inv_end=4)
 *
 *    In a reduced process tree, each XOR(tau,SEQ(...)) construct has at least one subtree (a pivot) that is not
 * optional. A pivot cannot be a sequential node itself. Secondly, observe that while non-root pivots are not
 * necessarily executed in a trace (due to their XOR(tau,SEQ()) parent), execution of the pivot is implied by the
 * execution of any sibling.
 *
 *    We usually use the last subtree in the optional sequence as pivot.
 *      - pivot = 3
 *
 *    Now with find_backward_dependent_nodes we search for the other nodes in that optional sequence. For that we start
 * at the pivot and search backwards for the first component which is smaller or equal to our pivot. This is where we
 *    "branch". So the component following that start_component is the first one in the optional branch.
 *      - start_component = 1
 *
 *    We now merge from start_component+1 till the pivot (pivot included) and end up with the following mapping
 *      - new_vertex_component_mapping = [0,1,3,3,4,5]
 *
 *    Analogously, for find_forward_dependent_nodes
 *      - end_component = 4
 *
 *    Next we merge from the pivot (pivot included) to end_component-1
 *      - new_vertex_component_mapping = [0,1,3,3,4,5]      (This has no effect in our running example)
 *
 *    Now we just transform the id's into consecutive ids
 *      - final new_vertex_component_mapping = [0,1,2,2,3,4]
 */
bool contains_skippable_components(const std::vector<int64_t>& min_from, const std::vector<int64_t>& max_to) {
  const bool has_inversion_max_to{!std::ranges::is_sorted(max_to)};
  const bool has_inversion_min_from{!std::ranges::is_sorted(min_from)};

  return has_inversion_max_to || has_inversion_min_from;
}

void merge_component_with_pivot(const std::vector<size_t>& vertex_component_mapping, int64_t component_id_to_merge,
                                int64_t pivot, equivalence_relation& equiv) {
  // merge components component_id_to_merge and pivot
  for (size_t offset{0}; offset < vertex_component_mapping.size(); ++offset) {
    if (ctl::cast<int64_t>(vertex_component_mapping[offset]) == component_id_to_merge) {
      equiv.add(vertex_component_mapping[offset], pivot);
    }
  }
}

void find_backward_dependent_nodes(const std::vector<size_t>& vertex_component_mapping, std::vector<int64_t>& max_to,
                                   int64_t pivot, equivalence_relation& equiv) {
  debug_assert(pivot < ctl::cast<int64_t>(max_to.size()));

  int64_t start_component{pivot - 1};
  while (start_component >= 0 && max_to[start_component] <= pivot) {
    start_component--;
  }
  for (int64_t component{start_component + 1}; component < pivot; component++) {
    debug_assert(component != pivot);
    merge_component_with_pivot(vertex_component_mapping, component, pivot, equiv);
  }
}

void find_forward_dependent_nodes(const std::vector<size_t>& vertex_component_mapping, std::vector<int64_t>& min_from,
                                  int64_t pivot, int64_t dfg_count, equivalence_relation& equiv) {
  debug_assert(dfg_count == ctl::cast<int64_t>(min_from.size()));

  // walk forward to find dependent nodes
  int64_t end_component{pivot + 1};
  while (end_component < dfg_count && min_from[end_component] >= pivot) {
    // depending node
    end_component++;
  }
  for (int64_t component{pivot + 1}; component < end_component - 1; component++) {
    debug_assert(component != pivot);
    merge_component_with_pivot(vertex_component_mapping, component, pivot, equiv);
  }
}

std::vector<size_t> search_and_process_pivots(std::vector<int64_t>& max_to, std::vector<int64_t>& min_from,
                                              std::vector<bool>& has_skipping_edges,
                                              const std::vector<size_t>& vertex_component_mapping, int64_t dfg_count) {
  equivalence_relation equiv{ctl::cast<size_t>(dfg_count)};

  for (int64_t current_component{0}; current_component < dfg_count; current_component++) {
    /*
     * We are not sure why we need both directions. All the test except "SequenceCutStrict Complex Test Case 1"
     * produce the correct result with either find_backward_dependent_nodes or find_forward_dependent_nodes.
     * Case 1 does not produce the correct result with find_forward_dependent_nodes but with
     * find_backward_dependent_nodes. We keep both direction anyhow because we believe that there must have been good
     * reason to include it in ProM and the version described in Sander's thesis contains both directions
     */
    // backward pivot
    if (const auto prev_component{current_component - 1}; current_component >= 1 &&
                                                          has_skipping_edges[current_component] &&
                                                          max_to[prev_component] == current_component) {
      // walk backward to find dependent nodes
      find_backward_dependent_nodes(vertex_component_mapping, max_to, current_component, equiv);
    }

    // forward pivot
    auto next_component{current_component + 1};
    if (current_component < dfg_count - 1 && has_skipping_edges[current_component] &&
        min_from[next_component] == current_component) {
      // forward pivot found
      find_forward_dependent_nodes(vertex_component_mapping, min_from, current_component, dfg_count, equiv);
    }
  }
  std::vector<size_t> new_vertex_component_mapping(vertex_component_mapping.size());
  std::ranges::transform(vertex_component_mapping, begin(new_vertex_component_mapping),
                         [map = equiv.mapping()](auto component) { return map[component]; });
  return new_vertex_component_mapping;
}

cut_t merge_optional(const directly_follows_graph& dfg, const cut_t& cut) {
  auto vertex_component_mapping = cut.second;
  static constexpr int64_t MAX_COMPONENT = std::numeric_limits<int64_t>::max();
  static constexpr int64_t MIN_COMPONENT = std::numeric_limits<int64_t>::min();
  // minFrom[x] the earliest activity set with an outgoing directly follows-edge to x
  std::vector<int64_t> min_from(cut.first, MAX_COMPONENT);
  // maxTo[x] the latest activity set from with an incoming directly follows-edge from x
  std::vector<int64_t> max_to(cut.first, MIN_COMPONENT);
  std::vector<bool> has_skipping_edges(cut.first, false);

  int64_t dfg_count{ctl::cast<int64_t>(cut.first)};

  /*
     Mark components which are skipped due to start vertices
     Components with start activities have the minimal possible incoming component
   */
  const auto& start_vertices = dfg[boost::graph_bundle].start_vertices;
  for (const auto& start_vertex : start_vertices) {
    auto comp_id = ctl::cast<int64_t>(vertex_component_mapping[start_vertex.first]);
    min_from[comp_id] = MIN_COMPONENT;
    for (int64_t i{0}; i < comp_id; i++) {
      has_skipping_edges[i] = true;
    }
  }

  /*
    Mark components which are skipped due to end vertices
    Components with an end activity have the maximal possible outgoing component
   */
  const auto& end_vertices = dfg[boost::graph_bundle].end_vertices;
  for (const auto& end_vertex : end_vertices) {
    auto comp_id = ctl::cast<int64_t>(vertex_component_mapping[end_vertex.first]);
    max_to[comp_id] = MAX_COMPONENT;
    for (int64_t i{comp_id + 1}; i < dfg_count; i++) {
      has_skipping_edges[i] = true;
    }
  }

  for (const auto& edge : dfg.m_edges) {
    const auto& source_comp = ctl::cast<int64_t>(vertex_component_mapping[edge.m_source]);
    const auto& target_comp = ctl::cast<int64_t>(vertex_component_mapping[edge.m_target]);
    min_from[target_comp] = std::min(min_from[target_comp], source_comp);
    max_to[source_comp] = std::max(max_to[source_comp], target_comp);
    // Mark components which are skipped due to optional events
    for (auto i{source_comp + 1}; i < target_comp; i++) {
      has_skipping_edges[i] = true;
    }
  }

  if (!contains_skippable_components(min_from, max_to)) {
    return cut;
  }

  // look for pivots
  auto new_vertex_component_mapping{
      search_and_process_pivots(max_to, min_from, has_skipping_edges, vertex_component_mapping, dfg_count)};

  // Clean mapping to get consecutive component ids
  std::vector<size_t> unique_list{new_vertex_component_mapping};
  std::sort(unique_list.begin(), unique_list.end());
  auto last = std::unique(unique_list.begin(), unique_list.end());
  unique_list.erase(last, unique_list.end());
  // Assign continuous ids
  std::unordered_map<size_t, size_t> index_map;
  for (auto i : unique_list) {
    index_map.emplace(i, index_map.size());
  }
  for (auto& elem : new_vertex_component_mapping) {
    elem = index_map.at(elem);
  }

  return cut_t(unique_list.size(), new_vertex_component_mapping);
}

std::vector<splittable_eventlog> compute_sub_eventlogs(inductive_miner_config& miner_config,
                                                       const directly_follows_graph& old_dfg, const cut_t& cut,
                                                       const common::execution_context& context) {
  auto sub_eventlog_context{context.create_sub_context("max_seq_cut: compute sub-eventlogs", {})};
  const auto activity_count{miner_config.eventlog().activity_domain_count()};

  const auto& [dfg_count, vertex_mapping]{cut};
  // Initialize the sub-graphs and create mappings from activity id to dfg id.
  const auto activity_dfg_mapping{cut_strategy::to_activity_dfg_map(cut, old_dfg, activity_count)};
  auto sub_eventlogs{miner_config.eventlog().split(activity_dfg_mapping)};
  debug_assert(sub_eventlogs.size() == dfg_count);
  return sub_eventlogs;
}

std::vector<directly_follows_graph> get_sub_dfs_from_old_dfg(const directly_follows_graph& old_dfg, const cut_t& cut) {
  auto sub_dfgs{sub_dfgs::build_sub_dfgs_from_components(old_dfg, cut, true)};

  // Set the DFG level properties for each sub DFG
  sub_dfgs::add_start_and_end_vertices_from_old_dfg(old_dfg, cut.second, sub_dfgs);
  sub_dfgs::accumulate_non_empty_traces_per_dfg(sub_dfgs.sub_dfgs);
  // Set empty trace count and correct trace count
  for (auto& dfg : sub_dfgs.sub_dfgs) {
    dfg[boost::graph_bundle].log.contains_empty_trace =
        old_dfg[boost::graph_bundle].log.trace_count - dfg[boost::graph_bundle].log.trace_count;
    dfg[boost::graph_bundle].log.trace_count = old_dfg[boost::graph_bundle].log.trace_count;
  }
  return sub_dfgs.sub_dfgs;
}

}  // namespace

cut_t max_seq_cut::find(const directly_follows_graph& dfg) {
  // Remember:
  // There is no non-trivial exclusive-choice cut,
  // i.e. the whole graph is weakly connected.

  const size_t num_vertices{boost::num_vertices(dfg)};

  std::vector<size_t> strong_component_mapping(num_vertices);
  const auto strong_component_count{boost::strong_components(  // NOLINT(clang-analyzer-core.StackAddressEscape)
      dfg, boost::make_iterator_property_map(strong_component_mapping.begin(), boost::get(boost::vertex_index, dfg)))};

  // Within the strong components all vertices are connected. Thus it suffices to check,
  // whether single elements / witnesses of distinct components are connected.
  // If so, they must be connected in one direction only (otherwise they would belong to
  // the same strong component), i.e. can be split by a sequence cut.
  // If they are not, then we have no information about their order, i.e. they cannot be
  // split by a sequence cut.
  std::vector<size_t> witnesses(strong_component_count);
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDelete)
  for (size_t i{0}; i < num_vertices; ++i) {
    witnesses[strong_component_mapping[i]] = i;
  }

  std::vector<std::vector<size_t>> distances(strong_component_count, std::vector<size_t>(strong_component_count, 0));
  // Note that dist(i,j) == 0 only iff i == j or the vertices are not connected at all.
  // If they were, their distance would be strictly greater than 0, which the bfs would
  // discover.

  // TODO (goulart.e): This procedure may be accelerated by constructing a new "condensed" graph
  // with the strong components as it's vertices. To add the edges, we have to iterate
  // over all the edges of `graph` only once. Afterwards we apply the bfs to this new
  // graph, thus iterating over (hopefully/potentially) much less edges -- all of this
  // once per witness. In contrast, at the moment we iterate over all edges of `graph`
  // -- once per witness.
  for (size_t i{0}; i < strong_component_count; ++i) {
    std::vector<size_t> distance_buffer(num_vertices, 0);
    boost::breadth_first_search(
        dfg, boost::vertex(witnesses[i], dfg),  // distance to witness i
        boost::visitor(boost::make_bfs_visitor(boost::record_distances(
            boost::make_iterator_property_map(distance_buffer.begin(), boost::get(boost::vertex_index, dfg)),
            boost::on_tree_edge()))));
    for (size_t j{0}; j < strong_component_count; ++j) {
      distances[i][j] = distance_buffer[witnesses[j]];
    }
  }

  graph groupings{strong_component_count};
  for (size_t c1{0}; c1 < strong_component_count; ++c1) {
    for (size_t c2{c1 + 1}; c2 < strong_component_count; ++c2) {
      if (distances[c1][c2] == 0 && distances[c2][c1] == 0) {
        boost::add_edge(c1, c2, groupings);
      }
    }
  }

  std::vector<size_t> component_mapping(strong_component_count);
  const auto component_count{boost::connected_components(groupings, component_mapping.data())};

  // Recycle the container
  for (auto& strong_component : strong_component_mapping) {
    strong_component = component_mapping[strong_component];
  }

  boost::adjacency_list<boost::hash_setS> sorting(component_count);
  for (size_t sc1{0}; sc1 < strong_component_count; ++sc1) {
    for (size_t sc2{sc1 + 1}; sc2 < strong_component_count; ++sc2) {
      auto component_1{strong_component_mapping[witnesses[sc1]]};
      auto component_2{strong_component_mapping[witnesses[sc2]]};
      if (component_1 == component_2) {
        continue;
      }

      if (distances[sc1][sc2] > 0) {
        boost::add_edge(component_1, component_2, sorting);
      } else {
        boost::add_edge(component_2, component_1, sorting);
      }
    }
  }

  std::vector<size_t> sort_mapping(component_count);
  boost::topological_sort(sorting, sort_mapping.rbegin());

  // Recycle the container
  for (auto& strong_component : strong_component_mapping) {
    strong_component = sort_mapping[strong_component];
  }

  return cut_t(component_count, strong_component_mapping);
}

max_seq_cut::apply_result max_seq_cut::apply(inductive_miner_config& miner_config,
                                             const directly_follows_graph& old_dfg, const cut_t& cut,
                                             const common::execution_context& context,
                                             const cube::execution::tracking::stop_token& stop_token) {
  auto apply_context{context.create_sub_context("max_seq_cut::apply", {})};
  auto merged_cut{merge_optional(old_dfg, cut)};

  /**
   * CPL-10426
   *
   * We observed an issue in production where the IM is stuck in the max_seq_cut. It was found that the number of
   * DFGs in the found cut was equal to one. Therefore we suspect we might be stuck in a loop where we merge the cut
   * an can apply max_seq_cut on it again. The if statement below detects this case and uses the old cut rather than
   * the merged cut with a single dfg in that case.
   */
  if (const auto& [dfg_count, _]{merged_cut}; dfg_count == 1) {
    merged_cut = cut;
  }

  stop_token.stop_execution_if_requested();
  auto sub_eventlogs{compute_sub_eventlogs(miner_config, old_dfg, merged_cut, context)};
  auto sub_dfgs{get_sub_dfs_from_old_dfg(old_dfg, merged_cut)};
  debug_assert(sub_eventlogs.size() == sub_dfgs.size());
  return {std::move(sub_eventlogs), std::move(sub_dfgs)};
}

process_tree::sequence max_seq_cut::from_dfgs(inductive_miner_config miner_config,
                                              max_seq_cut::apply_result logs_and_dfgs,
                                              const common::execution_context& context,
                                              inductive_miner_statistics& miner_statistics,
                                              const cube::execution::tracking::stop_token& stop_token) {
  auto& [sub_eventlogs, dfgs]{logs_and_dfgs};
  process_tree::sequence result{{}, dfgs.front()[boost::graph_bundle].log.trace_count};
  std::ranges::transform(dfgs, sub_eventlogs, std::back_inserter(result.children), [&](auto& dfg, auto eventlog) {
    debug_assert(dfg[boost::graph_bundle].log.trace_count == result.object_count);
    miner_config.eventlog() = std::move(eventlog);
    return inductive_miner_recurse(miner_config, dfg, context, miner_statistics, stop_token);
  });

  return result;
}

}  // namespace celonis::accelerator::operators::process
