#include "replay_aligned_variant.h"

#include <algorithm>
#include <map>
#include <ranges>

#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/filtered_graph.hpp>
#include <cpml/model/bpmn_graph.h>

#include "legacy_embedded_ctl/conversion.h"
#include "modules/common/exceptions.h"
#include "modules/operators/process/bpmn/replay_types.h"
#include "modules/operators/process/bpmn/replay_utils.h"
#include "partial_order_graph.h"

namespace celonis::accelerator::operators::process::align_model {
namespace {

/**
 * @brief Finds the next transition to be taken by looking ahead in the alignment and returns an iterator to it.
 *
 * Assumption: Here we assume that the BPMN model is "nicely behaved" in the sense that there is a single way to reach
 * each vertex from the current state. If this is not the case then we could not just look ahead in the alignment and
 * take the transition that appears first since it might be possible to reach that vertex through a different path in
 * the model.
 */
bpmn::transitions_t::const_iterator find_next_transition(const cpml::model::bpmn_graph& graph, const alignment_t& alignment,
                                                         size_t index, const bpmn::transitions_t& next_transitions) {
  const auto move{alignment.at(index)};
  legacy_embedded_debug_assert(move.move_type != alignment_move_type::LOG_MOVE && move.move_type != alignment_move_type::UNMAPPED_MOVE);
  const auto alignment_vertex_id{move.move_on_model.value()};
  // If unmapped, then this is (hopefully) empty
  std::vector outgoing_edges{graph.get_outgoing_edges(alignment_vertex_id)};

  using transition_iter_t = bpmn::transitions_t::const_iterator;

  std::vector<std::pair<transition_iter_t, size_t>> distances;
  // the search space starts from the next element in the alignment
  auto alignment_begin_iter{
      std::next(alignment.begin(), legacy_embedded_ctl::cast<decltype(alignment.begin())::difference_type>(index + 1))};

  // go over all the next transitions
  for (auto iter{next_transitions.cbegin()}; iter < next_transitions.cend(); iter++) {
    // get the iterator to the outgoing edge matching the transition
    const auto edge_iter{std::find_if(outgoing_edges.cbegin(), outgoing_edges.cend(), [&iter](const auto& edge) {
      legacy_embedded_debug_assert(iter->produced().size() == 1);
      return edge == iter->produced()[0];
    })};

    // should never be the case since the transitions generate tokens on the outgoing edges...
    legacy_embedded_debug_assert(edge_iter != outgoing_edges.end());

    auto target_vertex{edge_iter->get_target_id()};
    auto alignment_iter{std::find_if(alignment_begin_iter, alignment.end(), [target_vertex](const alignment_move& mv) {
      return mv.move_type != alignment_move_type::LOG_MOVE && mv.move_on_model == target_vertex;
    })};

    // compute distance from next alignment element to the next instance (if present) of the outgoing vertex in the
    // alignment
    distances.emplace_back(iter, std::distance(alignment_begin_iter, alignment_iter));
  }

  auto min_element{std::ranges::min_element(
      distances, [](const std::pair<transition_iter_t, size_t>& pair_a,
                    const std::pair<transition_iter_t, size_t>& pair_b) { return pair_a.second < pair_b.second; })};

  return min_element->first;
}

/*
 * useful mainly for LOG, UNMAPPED and SKIP components since these need to be generated separately.
 */
void add_components_to_graph(const replay_components_t& components, partial_order_graph& run_graph) {
  for (const auto& component : components) {
    const auto component_type{component.component_type};
    const auto& component_vertices{component.edges_as_vertices};
    legacy_embedded_debug_assert(component_vertices.size() >= 2);  // an edge needs at least two vertices

    boost::add_edge(component_vertices.at(0), component_vertices.at(1), partial_order_edge_properties{component_type},
                    run_graph);

    for (size_t idx{2}; idx < component_vertices.size(); idx++) {
      boost::add_edge(component_vertices.at(idx - 1), component_vertices.at(idx),
                      partial_order_edge_properties{component_type}, run_graph);
    }
  }
}

struct log_and_unmapped_components {
  replay_components_t log_components;
  replay_components_t unmapped_components;
};

auto sync_neighbor(auto it, const auto sentinel, const partial_order_graph& run_graph, const auto& is_parallel,
                   auto inc) {
  while (it != sentinel && (run_graph[*it].move_type != alignment_move_type::SYNC_MOVE ||
                            is_parallel(run_graph[*it].bpmn_vertex_id.value()))) {
    it = inc(it);
  }
  return it;
}

auto sync_predecessor(auto first, auto it, const partial_order_graph& run_graph, const auto& is_parallel) {
  return sync_neighbor(it, first, run_graph, is_parallel, std::ranges::prev);
}

auto sync_successor(auto it, auto last, const partial_order_graph& run_graph, const auto& is_parallel) {
  return sync_neighbor(it, last, run_graph, is_parallel, std::ranges::next);
}

auto get_parallel_test(const std::optional<cpml::model::bpmn::vertex_id_type>& bpmn_id,
                       const parallel_vertex_pairs<>& parallel_vertices) {
  return [&bpmn_id, &parallel_vertices](cpml::model::bpmn::vertex_id_type other_id) {
    return bpmn_id.has_value() && parallel_vertices.test(other_id, bpmn_id.value());
  };
}

log_and_unmapped_components construct_log_and_unmapped_components(const partial_order_graph& run_graph,
                                                                  const cpml::model::bpmn_graph& graph,
                                                                  const parallel_vertex_pairs<>& parallel_vertices) {
  // this could be a lot simpler if we created filtered views with std::ranges but that does not work with clang-14 :(
  static_assert(std::is_same_v<typename partial_order_graph::vertex_list_selector, boost::vecS>);
  legacy_embedded_debug_assert(run_graph[0].bpmn_vertex_id.has_value());
  legacy_embedded_debug_assert(is_start(graph.get_vertex(run_graph[0].bpmn_vertex_id.value())));

  replay_components_t log_components{};
  replay_components_t unmapped_components{};

  auto [first, past_end]{boost::vertices(run_graph)};

  if (std::distance(first, past_end) < 3) {  // besides start and end, there has to be at least one other move
    return {std::move(log_components), std::move(unmapped_components)};
  }
  const auto end_vertex_it{std::prev(past_end)};
  legacy_embedded_debug_assert(run_graph[*end_vertex_it].bpmn_vertex_id.has_value());
  legacy_embedded_debug_assert(is_end(graph.get_vertex(run_graph[*end_vertex_it].bpmn_vertex_id.value())));

  for (auto it{std::next(first)}; it != end_vertex_it; ++it) {
    const auto& [bpmn_vertex_id, move_type]{run_graph[*it]};
    const auto& bpmn_id{bpmn_vertex_id};  // making clang happy, so that we can use it in a lambda capture
    if (move_type != alignment_move_type::LOG_MOVE && move_type != alignment_move_type::UNMAPPED_MOVE) {
      continue;
    }
    const auto is_parallel{get_parallel_test(bpmn_id, parallel_vertices)};
    // find the preceding sync move that is not parallel
    auto prev_it{sync_predecessor(first, std::prev(it), run_graph, is_parallel)};
    // find the succeeding sync move that is not parallel
    const auto next_it{sync_successor(std::next(it), end_vertex_it, run_graph, is_parallel)};
    auto [components, edge_type]{move_type == alignment_move_type::LOG_MOVE
                                     ? std::make_pair(std::ref(log_components), edge_type::LOG)
                                     : std::make_pair(std::ref(unmapped_components), edge_type::UNMAPPED)};
    components.emplace_back(edges_as_vertices_t{*prev_it, *it, *next_it}, edge_type);
  }

  return {std::move(log_components), std::move(unmapped_components)};
}

using bpmn_to_partial_order_map_t = std::multimap<cpml::model::bpmn::vertex_id_type, partial_vertex_t>;
using map_iter_t = std::multimap<cpml::model::bpmn::vertex_id_type, partial_vertex_t>::const_iterator;

/**
 * Get the last added element to the multimap(order preserving) bpmn_to_partial_order, with the supplied key.
 */
map_iter_t get_last_added_for(const bpmn_to_partial_order_map_t::key_type& key,
                              const bpmn_to_partial_order_map_t& bpmn_to_partial_order) {
  auto [range_begin_iter, range_end_iter]{bpmn_to_partial_order.equal_range(key)};
  legacy_embedded_debug_assert(range_begin_iter != bpmn_to_partial_order.end());
  return std::prev(range_end_iter);
}

/**
 * Firing a bpmn transition consumes tokens ("edges") and produces new tokens. The consumed tokens/edges in the
 * bpmn-model correspond to edges in the partial_order_graph. This function takes bpmn edges consumed in a transition
 * and adds the corresponding edges to the partial_order_graph. Note that such bpmn edges, generated through replay
 * can only generate SYNC and MODEL edges since LOG, UNMAPPED and SKIP edges cannot be generated by directly replaying
 * on the model.
 */
template <typename BeginIter, typename EndIter>
void construct_edges_and_add_to_partial_order_graph(BeginIter edge_begin, EndIter edge_end,
                                                    cpml::model::bpmn::vertex_id_type edge_target,
                                                    alignment_move_type edge_target_move_type,
                                                    const bpmn_to_partial_order_map_t& bpmn_to_partial_order,
                                                    partial_order_graph& run_graph) {
  // source/target are either MODEL, SYNC or GATEWAY moves - in particular, not LOG or UNMAPPED move.
  const auto get_edge_type{[](alignment_move_type source, alignment_move_type target) -> edge_type {
    legacy_embedded_debug_assert(source != alignment_move_type::LOG_MOVE);
    legacy_embedded_debug_assert(target != alignment_move_type::LOG_MOVE);
    legacy_embedded_debug_assert(source != alignment_move_type::UNMAPPED_MOVE);
    legacy_embedded_debug_assert(target != alignment_move_type::UNMAPPED_MOVE);

    if (source == alignment_move_type::MODEL_MOVE || target == alignment_move_type::MODEL_MOVE) {
      return edge_type::MODEL;
    }

    return edge_type::SYNC;
  }};

  for (; edge_begin != edge_end; edge_begin++) {
    const auto& edge{*edge_begin};
    legacy_embedded_debug_assert(edge_target == edge.get_target_id());

    cpml::model::bpmn::vertex_id_type source_bpmn_id{edge.get_source_id()};
    cpml::model::bpmn::vertex_id_type target_bpmn_id{edge.get_target_id()};

    // if a self loop
    partial_vertex_t source_descriptor{
        source_bpmn_id != target_bpmn_id
            ? get_last_added_for(source_bpmn_id, bpmn_to_partial_order)->second
            : std::prev(get_last_added_for(source_bpmn_id, bpmn_to_partial_order), 1)->second};

    partial_vertex_t target_descriptor{get_last_added_for(target_bpmn_id, bpmn_to_partial_order)->second};

    alignment_move_type source_move_type{run_graph[source_descriptor].move_type};

    edge_type edge_type{get_edge_type(source_move_type, edge_target_move_type)};

    boost::add_edge(source_descriptor, target_descriptor, partial_order_edge_properties{edge_type}, run_graph);
  }
}  // namespace celonis::accelerator::operators::process::align_model

struct replay_result {
  partial_order_graph run_graph;            // only contains model and sync edges
  replay_components_t log_components;       // log components do not intersect with unmapped
  replay_components_t unmapped_components;  // unmapped components do not intersect with log
};

/**
 * Builds the partial order graph containing the SYNC, MODEL, LOG and UNMAPPED edges. Additionally the log and
 * unmapped components
 * @param graph bpmn graph
 * @param alignment aligned variant
 * @return
 */
replay_result build_partial_order_graph(const cpml::model::bpmn_graph& graph, const alignment_t& alignment,
                                        const parallel_vertex_pairs<>& parallel_vertices) {
  legacy_embedded_debug_assert(!alignment.empty());
  legacy_embedded_debug_assert(graph.is_single_object());

  const auto start_move{alignment.front()};
  const auto end_move{alignment.back()};
  legacy_embedded_debug_assert(start_move.move_type == alignment_move_type::GATEWAY_MOVE);
  legacy_embedded_debug_assert(end_move.move_type == alignment_move_type::GATEWAY_MOVE);

  const auto start_vertex_id{start_move.move_on_model.value()};
  const auto end_vertex_id{end_move.move_on_model.value()};

  legacy_embedded_debug_assert(is_start(graph.get_vertex(start_vertex_id)));
  legacy_embedded_debug_assert(is_end(graph.get_vertex(end_vertex_id)));

  // since a bpmn_vertex may be visited multiple times, this is a multimap. This way, we can also handle self loops.
  bpmn_to_partial_order_map_t bpmn_to_partial_order;
  partial_order_graph run_graph;

  partial_vertex_t start_descriptor{boost::add_vertex(
      partial_order_vertex_properties{start_vertex_id, start_move.move_type},
      run_graph)};

  bpmn_to_partial_order.emplace(legacy_embedded_ctl::cast<cpml::model::bpmn::vertex_id_type>(start_vertex_id), start_descriptor);

  // there should only ever be a single marking - if we get an XOR then we look ahead and see which path to take
  bpmn::marking_t marking{bpmn::get_initial_marking(graph)};
  bpmn::transitions_t next_transitions;

  // since this is an aligned trace - we should always be able to replay it
  for (size_t index{1}; index < alignment.size() - 1; index++) {
    const auto& move{alignment[index]};

    // corresponds to the alignment in that index == current_run_graph_vertex since
    // boost::adjacency_list VertexList == boost::vecS
    partial_vertex_t current_vertex_descriptor{
        boost::add_vertex(partial_order_vertex_properties{move.move_on_model, move.move_type}, run_graph)};

    // cannot replay LOG_MOVE or UNMAPPED_MOVE vertices
    if (move.move_type == alignment_move_type::LOG_MOVE || move.move_type == alignment_move_type::UNMAPPED_MOVE) {
      continue;
    }

    const auto current_bpmn_vertex_id{move.move_on_model.value()};

    // a single bpmn_vertex can of course be associated with multiple vertices in the partial order graph.
    // Additionally note that we do not store mapping for LOG and UNMAPPED moves because we use this mapping for
    // generating partial order graph edges from bpmn edges - but there are no bpmn edges for LOG/UNMAPPED moves
    bpmn_to_partial_order.emplace(current_bpmn_vertex_id, current_vertex_descriptor);

    // we need an enabled transition on the current_bpmn_vertex_id that we can fire in order to evolve the marking
    // (i.e. our position in the model)
    next_transitions = bpmn::get_enabled_vertex_transitions(graph, marking, current_bpmn_vertex_id);
    // NB: aligned, so there is always at least one transition. Usually, there is only one enabled transition. If
    // there are multiple, find the next by looking ahead in the alignment.
    bpmn::transition next_transition{next_transitions.size() > 1
                                         ? *find_next_transition(graph, alignment, index, next_transitions)
                                         : next_transitions.at(0)};
    marking = bpmn::fire(marking, next_transition);

    // produce edges from firing transition
    const auto& consumed_edges{next_transition.consumed()};

    // bpmn edges -> partial_order_graph edges
    construct_edges_and_add_to_partial_order_graph(consumed_edges.begin(), consumed_edges.end(),
                                                   current_bpmn_vertex_id, move.move_type, bpmn_to_partial_order,
                                                   run_graph);
  }

  if (!bpmn::end_marking_reached(graph, marking)) {
    throw common::internal_exception{"Failed aligned variant replay on bpmn graph: did not reach the end marking."};
  }

  partial_vertex_t end_descriptor{
      boost::add_vertex(partial_order_vertex_properties{end_vertex_id, end_move.move_type}, run_graph)};

  // should not have seen the end node before
  legacy_embedded_debug_assert(!bpmn_to_partial_order.contains(end_vertex_id));
  bpmn_to_partial_order.emplace(end_vertex_id, end_descriptor);

  // add end edge
  legacy_embedded_debug_assert(marking.size() == 1);
  construct_edges_and_add_to_partial_order_graph(marking.cbegin(), marking.cend(), end_vertex_id, end_move.move_type,
                                                 bpmn_to_partial_order, run_graph);

  auto [log_components,
        unmapped_components]{construct_log_and_unmapped_components(run_graph, graph, parallel_vertices)};
  add_components_to_graph(log_components, run_graph);
  add_components_to_graph(unmapped_components, run_graph);

  return {run_graph, std::move(log_components), std::move(unmapped_components)};
}

using run_graph_edges_t = std::vector<partial_edge_t>;

[[nodiscard]] replay_component to_result_component_type(const run_graph_edges_t& edges, edge_type component_type) {
  legacy_embedded_debug_assert(!edges.empty());

  if (edges.size() == 1) {
    const auto& edge{edges.at(0)};
    edges_as_vertices_t vertex_list = {edge.m_source, edge.m_target};
    return {{std::move(vertex_list)}, component_type};
  }

  // multiple edges
  edges_as_vertices_t vertex_list = {edges.at(0).m_source, edges.at(0).m_target};
  for (size_t idx{1}; idx < edges.size(); idx++) {
    legacy_embedded_debug_assert(edges.at(idx - 1).m_target == edges.at(idx).m_source);
    vertex_list.emplace_back(edges.at(idx).m_target);
  }
  return {std::move(vertex_list), component_type};
};

/**
 * Helper class to construct SYNC/MODEL edge components during DFS on partial order graph.
 * Each component contains adjacent edges where 'adjacency' depends on edge type since for MODEL edges,
 * we do not want a component to span across GATEWAY vertices.
 */
template <align_model::edge_type TYPE>
class dfs_edge_component_collector {
  static_assert(TYPE == edge_type::SYNC || TYPE == edge_type::MODEL);

 public:
  using component_t = run_graph_edges_t;
  using components_t = std::vector<component_t>;

  dfs_edge_component_collector(const cpml::model::bpmn_graph& bpmn_graph, const partial_order_graph& run_graph)
      : bpmn_graph_{bpmn_graph}, run_graph_{run_graph}, components_{} {}

  void add_edge(partial_edge_t edge) {
    legacy_embedded_debug_assert(run_graph_[edge].type == TYPE);

    if (components_.empty() || !are_adjacent_edges(components_.back().back(), edge)) {
      components_.emplace_back(1, edge);
    } else {
      components_.back().emplace_back(edge);
    }
  }

  [[nodiscard]] const components_t& get_components() const& { return components_; }
  [[nodiscard]] components_t get_components() && { return std::move(components_); }

 private:
  const cpml::model::bpmn_graph& bpmn_graph_;
  const partial_order_graph& run_graph_;
  components_t components_;

  [[nodiscard]] bool are_adjacent_edges(partial_edge_t edge1, partial_edge_t edge2) const {
    if constexpr (TYPE == edge_type::MODEL) {
      // model edge are not adjacent through gateways i.e edges 2->3 and 3->4 are not adjacent if 3 is a gateway
      legacy_embedded_debug_assert(run_graph_[edge1.m_target].bpmn_vertex_id.has_value());
      const cpml::model::bpmn::vertex_id_type target_1_bpmn_vertex_id{run_graph_[edge1.m_target].bpmn_vertex_id.value()};

      bool target_1_is_gateway(is_gateway(bpmn_graph_.get_vertex(target_1_bpmn_vertex_id).get_vertex_type()));
      const bool are_adjacent{!target_1_is_gateway && (edge1.m_target == edge2.m_source)};

      if (are_adjacent) {
        // make sure the underlying bpmn vertex ids are also consistent
        legacy_embedded_debug_assert(run_graph_[edge2.m_source].bpmn_vertex_id == target_1_bpmn_vertex_id);
      }
      return are_adjacent;

    } else if constexpr (TYPE == edge_type::SYNC) {
      const bool are_adjacent{edge1.m_target == edge2.m_source};

      if (are_adjacent) {
        // if adjacent, check that the bpmn edge ids are also valid
        legacy_embedded_debug_assert(run_graph_[edge1.m_target].bpmn_vertex_id.has_value());
        const cpml::model::bpmn::vertex_id_type target_1_bpmn_vertex_id{run_graph_[edge1.m_target].bpmn_vertex_id.value()};
        legacy_embedded_debug_assert(run_graph_[edge2.m_source].bpmn_vertex_id == target_1_bpmn_vertex_id);
      }

      return are_adjacent;
    }
  }
};

template <edge_type FILTER_TO_EDGES>
struct edge_type_filter {
  edge_type_filter() = default;
  [[nodiscard]] bool operator()(const partial_edge_t& edge) const {
    const auto* property_ptr{static_cast<const partial_order_edge_properties*>(edge.get_property())};
    const auto edge_type{property_ptr->type};
    return edge_type == FILTER_TO_EDGES;
  }
};

template <edge_type TYPE>
class partial_order_graph_dfs_visitor : public boost::default_dfs_visitor {
 public:
  explicit partial_order_graph_dfs_visitor(std::shared_ptr<dfs_edge_component_collector<TYPE>> edge_collector)
      : edge_collector_{std::move(edge_collector)} {}

  template <typename GRAPH>
  void examine_edge(const partial_edge_t& edge, const GRAPH& run_graph) {
    const auto e_type{run_graph[edge].type};

    if (e_type == TYPE) {
      edge_collector_->add_edge(edge);
    }
  }

 private:
  std::shared_ptr<dfs_edge_component_collector<TYPE>> edge_collector_;
};

using timestamp_join_map_t = std::vector<size_t>;

/**
 * Finds the maximum input to the target vertex in the partial order graph: by the maximum input, we mean the
 * predecessor of target_vertex that was executed latest. We can do this by just looking at the vertex descriptor
 * of the input vertices to the target_vertex since by construction, the partial order graph must ensure this
 * property.
 */
partial_vertex_t compute_max_input_vertex(partial_vertex_t target_vertex, const partial_order_graph& run_graph,
                                          const timestamp_join_map_t& timestamp_join_map) {
  legacy_embedded_debug_assert(boost::in_degree(target_vertex, run_graph) > 0);
  const auto [begin_iter, end_iter]{boost::in_edges(target_vertex, run_graph)};

  // get edge with maximum timestamp
  const partial_in_edge_iter max_edge_iter{
      std::max_element(begin_iter, end_iter,
                       [&run_graph = std::as_const(run_graph), &timestamp_join_map = std::as_const(timestamp_join_map)](
                           const partial_edge_t& edge1, const partial_edge_t& edge2) {
                         const partial_vertex_t edge1_source{boost::source(edge1, run_graph)};
                         const partial_vertex_t edge2_source{boost::source(edge2, run_graph)};

                         return timestamp_join_map.at(edge1_source) < timestamp_join_map.at(edge2_source);
                       })};

  return timestamp_join_map.at(boost::source(*max_edge_iter, run_graph));
}

using alignment_idx_to_log_idx_t = std::unordered_map<size_t, size_t>;

/**
 * Assign an order to each non-model activity in the alignment - these are the activities that also occur in the input
 * eventlog in this exact same order. We need to do this because we want to generate the timestamp join map relative
 * to the order of the activities in the input activity column.
 *
 * The assumption we need here is that the alignment cannot reorder activities in the eventlog - which is the case for
 * any valid alignment.
 */
alignment_idx_to_log_idx_t map_alignment_activities_to_eventlog_idx(const alignment_t& alignment) {
  size_t num_seen_activities{0};
  alignment_idx_to_log_idx_t result;

  for (size_t idx{0}; idx < alignment.size(); idx++) {
    const auto& move{alignment.at(idx)};
    if (move.move_on_log) {
      // Sync, log or unmapped moves
      result.emplace(idx, num_seen_activities);
      num_seen_activities++;
    }
  }
  return result;
}

/**
 * Check whether each vertex i only has inputs j, such that j < i.
 **/
[[nodiscard]] bool is_ordered(const partial_order_graph& run_graph) {
  const auto graph_iter{boost::vertices(run_graph)};
  return std::all_of(graph_iter.first, graph_iter.second,
                     [&run_graph = std::as_const(run_graph)](partial_vertex_t vertex) {
                       const auto& input_edges{boost::in_edges(vertex, run_graph)};
                       return std::all_of(input_edges.first, input_edges.second,
                                          [&vertex](const partial_edge_t& edge) { return vertex > edge.m_source; });
                     });
}

/**
 * Iterate over the vertices in the Partial Order graph and compute the timestamp join map. By a 'timestamp join map'
 * here, we mean a mapping from each item x in the alignment to an activity in the input log e.g. the start
 * vertex gets the timestmap 0 - meaning that we join it to the first activity in the input log. We can do this,
 * since the alignment cannot reorder activities in the input log.
 *
 * @param alignment a vector containing the alignment. We assume that the first element in the alignment array
 * corresponds to the 'start' vertex and the last to the 'end' vertex.
 *
 * @param run_graph a run graph containing at least the SYNC, MODEL LOG and UNMAPPED edges. SKIP edges do not
 * contribute to timestamps since they skip over model-move components.
 */
timestamp_join_map_t generate_timestamp_join_map(const partial_order_graph& run_graph,
                                                 const cpml::model::bpmn_graph& bpmn_graph, const alignment_t& alignment) {
  legacy_embedded_debug_assert(!alignment.empty());
  // By construction of the partial order graph, vertex id / descriptor (boost::vecS) IS the index in the vector
  legacy_embedded_debug_assert(std::all_of(boost::vertices(run_graph).first, boost::vertices(run_graph).second,
                           [index = size_t{0}](const auto& vertex_id) mutable {
                             const auto index_equals_descriptor{index == vertex_id};
                             index++;
                             return index_equals_descriptor;
                           }));

  if (!is_ordered(run_graph)) {
    throw common::internal_exception{"ALIGN_MODEL: Cannot compute timestamp joins with unordered partial graph."};
  }

  const auto alignment_idx_to_log_idx{map_alignment_activities_to_eventlog_idx(alignment)};

  if (alignment_idx_to_log_idx.empty()) {
    // if there are no non-model-move activities then the trace is empty and we cannot generate any 'timestamps'
    return timestamp_join_map_t(alignment.size(), 0);
  }

  // start gets joined to first activity in the log i.e. idx 0
  const size_t start_vertex_timestamp_join{0};
  timestamp_join_map_t timestamp_join_map(alignment.size(), start_vertex_timestamp_join);

  // skip start
  auto [v_begin, v_end]{boost::vertices(run_graph)};
  for (const auto target_vertex : boost::make_iterator_range(std::next(v_begin), v_end)) {
    // the timestamp join at the vertex depends on the timestamp joins of all the incoming edges into the vertex
    const auto bpmn_vertex_id{run_graph[target_vertex].bpmn_vertex_id};
    if (run_graph[target_vertex].move_type == alignment_move_type::LOG_MOVE ||
        run_graph[target_vertex].move_type == alignment_move_type::UNMAPPED_MOVE) {
      timestamp_join_map.at(target_vertex) = alignment_idx_to_log_idx.at(target_vertex);
      continue;
    }
    const auto& bpmn_vertex{bpmn_graph.get_vertex(bpmn_vertex_id.value())};
    const auto move_type{run_graph[target_vertex].move_type};

    // we go over all vertices and compute the 'timestamp' by looking at the input edges - by construction of the
    // partial order graph, we should have already processed all the input vertices of a vertex 't' when processing
    // 't' itself.
    const auto target_timestamp{
        std::visit(legacy_embedded_ctl::overloaded{[&](const cpml::model::bpmn::task& /*task*/) {
                                     // if non-model-move activity, timestamp join is identity
                                     if (move_type != alignment_move_type::MODEL_MOVE) {
                                       legacy_embedded_debug_assert(move_type == alignment_move_type::SYNC_MOVE);
                                       return alignment_idx_to_log_idx.at(target_vertex);
                                     }
                                     // otherwise, this is a model move activity - without SKIP edges this should only
                                     // have a single incoming edge
                                     legacy_embedded_debug_assert(std::distance(boost::in_edges(target_vertex, run_graph).first,
                                                                boost::in_edges(target_vertex, run_graph).second) == 1);
                                     return compute_max_input_vertex(target_vertex, run_graph, timestamp_join_map);
                                   },
                                   [&](const auto& /*parallel_exclusive_or_end*/) {
                                     return compute_max_input_vertex(target_vertex, run_graph, timestamp_join_map);
                                   },
                                   [&](const cpml::model::bpmn::start& /*start*/) {
                                     legacy_embedded_ctl::assert_unreachable();
                                     return size_t{0};
                                   }},
                   bpmn_vertex.get_vertex_type())};

    timestamp_join_map.at(target_vertex) = target_timestamp;
  }
  return timestamp_join_map;
}

struct sync_and_model_components {
  std::vector<run_graph_edges_t> sync_components;
  std::vector<run_graph_edges_t> model_components;
};

/**
 * Create SYNC and MODEL edge components by performing DFS (two separate DFS iterations) over the filtered partial
 * order graph (once for SYNC, once for MODEL). We filter so as to get larger SYNC/MODEL components.
 */
sync_and_model_components create_sync_and_model_components(const partial_order_graph& run_graph,
                                                           const cpml::model::bpmn_graph& bpmn_graph) {
  // use DFS to build the sync and model edges components
  auto indexmap = boost::get(boost::vertex_index, run_graph);
  auto colormap = boost::make_vector_property_map<boost::default_color_type>(indexmap);

  const auto sync_components_collector{
      std::make_shared<dfs_edge_component_collector<edge_type::SYNC>>(bpmn_graph, run_graph)};

  edge_type_filter<edge_type::SYNC> sync_filter{};
  edge_type_filter<edge_type::MODEL> model_filter{};

  boost::filtered_graph<partial_order_graph, edge_type_filter<edge_type::SYNC>> sync_edge_graph{run_graph, sync_filter};

  boost::filtered_graph<partial_order_graph, edge_type_filter<edge_type::MODEL>> model_edge_graph{run_graph,
                                                                                                  model_filter};
  const auto model_components_collector{
      std::make_shared<dfs_edge_component_collector<edge_type::MODEL>>(bpmn_graph, run_graph)};

  // SYNC and MODEL dfs calls
  partial_order_graph_dfs_visitor sync_visitor{sync_components_collector};
  boost::depth_first_search(sync_edge_graph, sync_visitor, colormap, 0);

  partial_order_graph_dfs_visitor model_visitor{model_components_collector};
  boost::depth_first_search(model_edge_graph, model_visitor, colormap, 0);

  auto sync_components_edges{std::move(*sync_components_collector).get_components()};
  auto model_components_edges{std::move(*model_components_collector).get_components()};

  return {std::move(sync_components_edges), std::move(model_components_edges)};
}

}  // namespace

replay_result_type replay_aligned_variant(const cpml::model::bpmn_graph& bpmn_graph, const alignment_t& aligned_variant,
                                          const parallel_vertex_pairs<>& parallel_vertices) {
  // get the run_graph with model and sync edges and additionally the log_edges
  auto [run_graph_, log_components,
        unmapped_components]{build_partial_order_graph(bpmn_graph, aligned_variant, parallel_vertices)};
  auto& run_graph{run_graph_};  // for lambda capture below

  const auto [sync_components_edges, model_components_edges]{create_sync_and_model_components(run_graph, bpmn_graph)};

  // generate the SKIP edges from model edge components - note that we may produce multiple SKIP components between the
  // same vertices, if there are multiple model components
  std::vector<replay_component> skip_components;
  for (const auto& model_component : model_components_edges) {
    if (model_component.size() > 1) {
      // each skip edge component contains a single edge, skipping over the 'adjacent' model edges
      const auto& first_model_edge{model_component.front()};
      const auto& last_model_edge{model_component.back()};

      const auto source_vertex{first_model_edge.m_source};
      const auto target_vertex{last_model_edge.m_target};

      // add skip edge to graph
      boost::add_edge(source_vertex, target_vertex, partial_order_edge_properties{edge_type::SKIP}, run_graph);

      skip_components.emplace_back(edges_as_vertices_t{source_vertex, target_vertex}, edge_type::SKIP);
    }
  }

  // the L1_MISSING components are almost the same as the model components, except that we do not connect to gateways
  auto missing_components_edges_size{model_components_edges.size()};

  const size_t num_components{sync_components_edges.size() + model_components_edges.size() + log_components.size() +
                              unmapped_components.size() + skip_components.size() + missing_components_edges_size};

  replay_components_t result_components(num_components);
  replay_components_t::iterator next_component_start_iter{result_components.begin()};

  // take MODEL and SYNC components and transform them into vertex based components i.e. component{ A->B, B->C } becomes
  // {A, B, C} (full edges are not required)
  next_component_start_iter =
      std::transform(sync_components_edges.begin(), sync_components_edges.end(), next_component_start_iter,
                     [](const run_graph_edges_t& edges) { return to_result_component_type(edges, edge_type::SYNC); });

  const auto model_components_first{next_component_start_iter};
  next_component_start_iter =
      std::transform(model_components_edges.begin(), model_components_edges.end(), next_component_start_iter,
                     [](const run_graph_edges_t& edges) { return to_result_component_type(edges, edge_type::MODEL); });
  const auto model_components_last{next_component_start_iter};

  next_component_start_iter = std::copy(std::make_move_iterator(skip_components.begin()),
                                        std::make_move_iterator(skip_components.end()), next_component_start_iter);

  next_component_start_iter = std::copy(std::make_move_iterator(log_components.begin()),
                                        std::make_move_iterator(log_components.end()), next_component_start_iter);

  next_component_start_iter = std::copy(std::make_move_iterator(unmapped_components.begin()),
                                        std::make_move_iterator(unmapped_components.end()), next_component_start_iter);

  // Add the L1_MISSING components: They are mostly the same as the model components, but if the first or last vertex is
  // a gateway, then we need to look for the next synchronous, non-parallel move (or the start/end gateway)
  const auto first_last{boost::vertices(run_graph)};
  next_component_start_iter = std::transform(
      model_components_first, model_components_last, next_component_start_iter,
      [&run_graph, &parallel_vertices, first = first_last.first, last = first_last.second](auto component) {
        legacy_embedded_debug_assert(component.component_type == edge_type::MODEL);
        component.component_type = edge_type::L1_MISSING;
        const auto front_it{std::ranges::find(first, last, component.edges_as_vertices.front())};
        const auto back_it{std::ranges::find(front_it, last, component.edges_as_vertices.back())};
        const auto first_sync_it{sync_predecessor(
            first, front_it, run_graph, get_parallel_test(run_graph[*front_it].bpmn_vertex_id, parallel_vertices))};
        component.edges_as_vertices.front() = *first_sync_it;
        const auto last_sync_it{
            sync_successor(back_it, std::prev(last), run_graph,
                           get_parallel_test(run_graph[*back_it].bpmn_vertex_id, parallel_vertices))};
        component.edges_as_vertices.back() = *last_sync_it;
        return component;
      });

  legacy_embedded_debug_assert(next_component_start_iter == result_components.end());

  // ensure that results are always generated in a certain order to ensure output stability
  std::ranges::sort(result_components);

  return {std::move(result_components), generate_timestamp_join_map(run_graph, bpmn_graph, aligned_variant)};
}

}  // namespace celonis::accelerator::operators::process::align_model
