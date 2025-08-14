#pragma once

#include <memory>
#include <numeric>
#include <vector>

#include <boost/graph/depth_first_search.hpp>

#include <cpml/conformance/alignment_types.h>
#include <cpml/model/bpmn_graph_fwd.h>
#include <ctl/hash.h>

#include "align_model_types.h"
#include "partial_order_graph.h"

namespace celonis::accelerator::operators::process::align_model {
/**
 * Replaying a single aligned variant on a model produces 5 basic types of edge components. The sync and model edges can
 * be computed by doing Depth-First-Search over a partial run graph only containing the edges of the corresponding type:
 * 1. Sync edge components: each component is formed by adjacent, incl. gateways, sync edges in the partial run
 * 2. Model edge components: each component is formed by adjacent, excl. gateways, model edges in the partial run
 * 3. Skip edge components: these are formed by adding an edge from the beginning of a model component to the end if the
 *    corresponding model edge component contains multiple edges
 * 4. Log edge components: each component just contains the 2 edges for a single LOG_MOVE in the alignment
 * 5. Unmapped components: each component contains the 2 edges for a single UNMAPPED_MOVE. Additionally, UNMAPPED
 *    components do not overlap with log components i.e. an UNMAPPED component will never have an edge to/from a
 *    LOG component and vice-versa.
 *
 * Given these edge components arranged in some order, gives us a separate edge class id for each component of
 * a variant since each component should get a separate edge_class_id.
 *
 * The LOG and UNMAPPED components can be built directly when replaying. For the MODEL and SYNC components,
 * we first build the Partial Order Graph and then traverse (e.g. DFS) it to discover these components. From the model
 * components, we then build the SKIP components.
 *
 * In addition, we also produce additional edge types:
 * 6. L1_MISSING: same as the model component but does not join to gateways
 * 7. L1_EXCLUSIVE_VIOLATION, the latter of which
 * is produced for activities that only have a log move meaning meaning that we violated an exclusive gateway.
 */

/** a 'replay_component' contains adjacent edges i.e. edges in the same component, therefore, we can just represent
 these as a vector of vertices e.g. {2, 3, 4, 5} means 3 edges 2->3, 3->4, 4->5
 */
using edges_as_vertices_t = std::vector<partial_vertex_t>;

/* To compute the join between ALIGNMENT and eventlog we compute relative offsets from the alignment to the preceding
 * move to take the timestamp from*/
using alignment_index_t = alignment_t::size_type;
using alignment_to_preceding_move_join_map_t = std::vector<alignment_index_t>;

using log_idx_t = size_t;  // This is an offset into a row, however since this can never be negative we use size_t here
                           // instead of row_id
using alignment_idx_to_log_idx_t = std::unordered_map<alignment_index_t, log_idx_t>;

struct replay_component {
  edges_as_vertices_t edges_as_vertices{};
  edge_type component_type{};  // all edges are of the same type

  replay_component() = default;

  replay_component(edges_as_vertices_t edges, edge_type type)
      : edges_as_vertices{std::move(edges)}, component_type{type} {}

  [[nodiscard]] edges_as_vertices_t::size_type size() const { return edges_as_vertices.size(); }

  [[nodiscard]] bool operator==(const replay_component& other) const = default;

  [[nodiscard]] std::strong_ordering operator<=>(const replay_component& other) const {
    // define ordering over edge_type
    const auto lhs{static_cast<std::underlying_type_t<edge_type>>(component_type)};
    const auto rhs{static_cast<std::underlying_type_t<edge_type>>(other.component_type)};
    return std::tie(lhs, edges_as_vertices) <=> std::tie(rhs, other.edges_as_vertices);
  }
};

struct replay_component_hasher {
  size_t operator()(const replay_component& component) const {
    size_t hash{ctl::hash_range(component.edges_as_vertices)};
    ctl::hash_combine(hash, component.component_type);
    return hash;
  }
};

using replay_components_t = std::vector<replay_component>;

/**
 * Represents the result of replaying a single variant on the model.
 * The type is immutable: this allows us to compute number of rows/edges in the components on construction.
 */
class replay_result_type {
 public:
  replay_result_type() = default;

  replay_result_type(replay_components_t components, alignment_to_preceding_move_join_map_t alignment_to_preceding_move,
                     alignment_idx_to_log_idx_t alignment_idx_to_log_idx)
      : components_{std::move(components)},
        alignment_to_preceding_move_{std::move(alignment_to_preceding_move)},
        alignment_idx_to_log_idx_{std::move(alignment_idx_to_log_idx)},
        num_rows_{compute_num_rows(components_)} {}

  [[nodiscard]] const replay_components_t& components() const { return components_; }

  [[nodiscard]] const alignment_to_preceding_move_join_map_t& alignment_to_preceding_move() const {
    return alignment_to_preceding_move_;
  }

  [[nodiscard]] const alignment_idx_to_log_idx_t& alignment_idx_to_log_idx() const { return alignment_idx_to_log_idx_; }

  [[nodiscard]] size_t num_edge_components() const { return components_.size(); }

  /**
   * @brief returns rows required for this result: i.e. sum of edges in all components
   */
  [[nodiscard]] size_t num_rows() const { return num_rows_; }

 private:
  replay_components_t components_;
  alignment_to_preceding_move_join_map_t alignment_to_preceding_move_;
  alignment_idx_to_log_idx_t alignment_idx_to_log_idx_;
  size_t num_rows_{0};

  [[nodiscard]] static size_t compute_num_rows(const replay_components_t& components) {
    return std::accumulate(
        components.begin(), components.end(), size_t{0},
        [](const size_t accumulated, const replay_component& component) { return accumulated + component.size(); });
  }
};

/**
 * @brief Replays a single aligned variant on a bpmn model and generates the corresponding four different types of edge
 * components (if present). Additionally, we also produce 'timestamps' (read joins back into the alignment itself i.e.
 * if we START, XOR1, A(SYNC), XOR2, END then START and XOR1 join back to A) on all vertices in the alignment. In
 * particular, the start vertex gets the timestamp of the first non-model activity and the end activity gets the
 * timestamp of last non-model activity.
 *
 * @param input_bpmn_graph bpmn graph
 * @param aligned_variant a variant from the eventlog, aligned to the petri-net/bpmn
 * @param parallel_vertices contains relations between BPMN nodes such as parallel or exclusive; relevant for log edges
 * @return replay_result_type containing the different edge components and the 'timestamps'. Note that the integers in
 * the produced result components refer to offsets into the provided 'aligned_variant' and do not refer to the indices
 * of the bpmn_graph.
 */
[[nodiscard]] replay_result_type replay_aligned_variant(const cpml::model::bpmn_graph& input_bpmn_graph,
                                                        alignment_view_t aligned_variant,
                                                        const cpml::conformance::behavioral_relations& relations);

}  // namespace celonis::accelerator::operators::process::align_model
