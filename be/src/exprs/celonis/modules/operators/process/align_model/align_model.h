#pragma once

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cpml/model/bpmn_graph.h>

#include "align_model_statistics.h"
#ifndef CELOSTAR
#include "modules/cube/query_scope_fwd.h"
#include "modules/cube/table_registry/input_dependencies.h"
#include "modules/cube/table_registry/table_registry.h"
#endif
#include "modules/cube/variant_trace_cache_manager.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/row_id.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/table_group.h"
#ifndef CELOSTAR
#include "modules/operators/framework/cached_operator_fwd.h"
#endif
#include "modules/operators/process/alignment/log_aligner.h"
#include "modules/operators/process/bpmn/bpmn_to_pn.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/bpmn/bpmn_from_proto.h"

namespace celonis::accelerator::operators::process::align_model {
// avoid circular includes
class replay_result_type;

// For now define these as constants, as we plan to use these in a table group anyway there should be no chance of name
// collision
constexpr std::string_view INTERNAL_ALIGNMENT_TABLE_NAME{"ALIGNMENT"};
constexpr std::string_view USER_VISIBLE_ALIGNMENT_TABLE_NAME{INTERNAL_ALIGNMENT_TABLE_NAME};
constexpr std::string_view INTERNAL_ASSOCIATION_TABLE_NAME{"ASSOCIATION"};
constexpr std::string_view USER_VISIBLE_ASSOCIATION_TABLE_NAME{INTERNAL_ASSOCIATION_TABLE_NAME};
constexpr std::string_view INTERNAL_EDGE_CLASS_TABLE_NAME{"EDGE_CLASS"};
constexpr std::string_view USER_VISIBLE_EDGE_CLASS_TABLE_NAME{INTERNAL_EDGE_CLASS_TABLE_NAME};
constexpr std::string_view TABLE_GROUP_NAME{"ALIGN_MODEL_GROUP"};

// alignments columns
constexpr std::string_view ALIGNMENT_MODEL_VERTEX_ID{"MODEL_VERTEX_ID"};
constexpr std::string_view ALIGNMENT_ACTIVITY_LABEL{"VERTEX_LABEL"};
constexpr std::string_view ALIGNMENT_MOVE_TYPE{"MOVE_TYPE"};
// association columns
constexpr std::string_view ASSOCIATION_COLUMN_NAME{"EDGE_CLASS"};
// edge class columns
constexpr std::string_view EDGE_CLASS_ID{"ID"};
constexpr std::string_view EDGE_CLASS_TYPE{"TYPE"};

struct align_model_config {
  static constexpr size_t ALIGN_MODEL_GRAIN_SIZE{1u << 15};

  [[nodiscard]] static align_model_config make(
      std::string pruned_variant_cache_key, cube::variant_trace_cache_manager* trace_cache_manager,
      const std::optional<alignment::log_aligner_config>& aligner_cfg = std::nullopt);

  size_t grain_size{};
  std::string pruned_variant_cache_key;
  cube::variant_trace_cache_manager* variant_trace_cache_manager_instance;
  alignment::log_aligner_config log_aligner_cfg;
};

using variants = memory::cache::variant_trace_cache_t;

enum class alignment_move_type { UNMAPPED_MOVE, LOG_MOVE, MODEL_MOVE, SYNC_MOVE, GATEWAY_MOVE };

struct alignment_move {
  alignment_move_type move_type{alignment_move_type::UNMAPPED_MOVE};
  std::optional<row_id> move_on_log{std::nullopt};
  std::optional<cpml::model::bpmn::vertex_id_type> move_on_model{std::nullopt};
  auto operator<=>(const alignment_move&) const noexcept = default;  // NOLINT(modernize-use-nullptr)
};

using alignment_t = std::vector<alignment_move>;
using alignments_t = std::vector<std::optional<alignment_t>>;

using edge_class_id_t = row_id;
enum class edge_type { SYNC, MODEL, SKIP, LOG, UNMAPPED, L1_MISSING };

struct alignment_move_type_strings {
  constexpr static std::string_view GATEWAY{"GATEWAY_MOVE"};
  constexpr static std::string_view SYNC{"SYNC_MOVE"};
  constexpr static std::string_view MODEL{"MODEL_MOVE"};
  constexpr static std::string_view LOG{"LOG_MOVE"};
  constexpr static std::string_view UNMAPPED{"UNMAPPED_MOVE"};
};

[[nodiscard]] constexpr static std::string_view alignment_move_to_string(alignment_move_type move_type) {
  switch (move_type) {
    case alignment_move_type::GATEWAY_MOVE:
      return alignment_move_type_strings::GATEWAY;
    case alignment_move_type::UNMAPPED_MOVE:
      return alignment_move_type_strings::UNMAPPED;
    case alignment_move_type::LOG_MOVE:
      return alignment_move_type_strings::LOG;
    case alignment_move_type::MODEL_MOVE:
      return alignment_move_type_strings::MODEL;
    case alignment_move_type::SYNC_MOVE:
      return alignment_move_type_strings::SYNC;
    default:
      legacy_embedded_ctl::assert_unreachable();
  }
}

struct edge_type_strings {
  constexpr static std::string_view SYNC{"SYNC_EDGE"};
  constexpr static std::string_view MODEL{"MODEL_EDGE"};
  constexpr static std::string_view SKIP{"SKIP_EDGE"};
  constexpr static std::string_view LOG{"LOG_EDGE"};
  constexpr static std::string_view UNMAPPED{"UNMAPPED_EDGE"};
  constexpr static std::string_view L1_MISSING{"L1_MISSING"};
};

[[nodiscard]] constexpr std::string_view edge_type_to_string(edge_type edge) {
  switch (edge) {
    case edge_type::SYNC:
      return edge_type_strings::SYNC;
    case edge_type::MODEL:
      return edge_type_strings::MODEL;
    case edge_type::LOG:
      return edge_type_strings::LOG;
    case edge_type::SKIP:
      return edge_type_strings::SKIP;
    case edge_type::UNMAPPED:
      return edge_type_strings::UNMAPPED;
    case edge_type::L1_MISSING:
      return edge_type_strings::L1_MISSING;
    default:
      legacy_embedded_ctl::assert_unreachable();
  }
}

using replay_results_t = std::vector<std::optional<replay_result_type>>;

/**
 * @brief Encapsulates if two BPMN vertices are "parallel" (concurrent)
 * @tparam ALLOCATOR
 */
template <typename ALLOCATOR = std::allocator<std::array<cpml::model::bpmn::vertex_id_type, 2>>>
class parallel_vertex_pairs {
 public:
  parallel_vertex_pairs() = default;
  explicit parallel_vertex_pairs(const ALLOCATOR& allocator) : data_(allocator) {}
  /**
   * @brief add a pair of mutually parallel vertices to the internal data structure
   *
   * @param i one BPMN vertex id
   * @param j another BPMN vertex id
   *
   * Note that the order of the two arguments does not matter, as the "parallel" relation is symmetric
   */
  void add(cpml::model::bpmn::vertex_id_type i, cpml::model::bpmn::vertex_id_type j) {
    data_.emplace(std::array{std::min(i, j), std::max(i, j)});
  }

  /**
   * @brief test whether two BPMN vertex ids are parallel
   *
   * @param i a BPMN vertex id
   * @param j another BPMN vertex id
   * @return true if the two input BPMN vertex ids are parallel, else false
   *
   * Note that the order of the two arguments does not matter, as the "parallel" relation is symmetric
   */
  [[nodiscard]] bool test(cpml::model::bpmn::vertex_id_type i, cpml::model::bpmn::vertex_id_type j) const {
    return data_.contains(std::array{std::min(i, j), std::max(i, j)});
  }

 private:
  struct hash {
    size_t operator()(const std::array<cpml::model::bpmn::vertex_id_type, 2>& v) const { return legacy_embedded_ctl::hash_range(v); }
  };
  std::unordered_set<std::array<cpml::model::bpmn::vertex_id_type, 2>, hash, std::ranges::equal_to, ALLOCATOR> data_{};
};

/**
 * @brief Aligns the variants in the variants log with the petri net model
 *
 * @param variants The variants to align
 * @param petri_net A Petri net
 * @param mapper The mapper from Petri net labels to label ids. Needs to be initialized with the activity column dict.
 * @param config Configuration parameters of the align model algorithm
 * @param context The execution context we're running in
 * @return alignments_t The alignments of all variants, and a binary relation of parallel activities
 *
 * Note: We compute the behavioral profile for the relaxation-labeling, and extract the parallel relation from there.
 */
std::pair<alignments_t, parallel_vertex_pairs<>> align_model(const memory::cache::variant_trace_cache_t& variants,
                                                             const bpmn::bpmn_to_petri_net_result_t& result,
                                                             const align_model_config& config,
                                                             align_model_statistics& stats,
                                                             const std::string& activity_table_name,
                                                             const common::execution_context& context);
/**
 * @brief Replays the aligned variants on the model creating their partial execution orders.
 * Also creates joins for all synchronous and model moves to the alignments
 *
 * @param petri_net A safe Petri net
 * @param alignment An alignment table as produced by @see align_model
 * @param config Configuration parameters of the align model algorithm
 * @return replay_results_t The partial order executions of all variants as well as groupers and edge types
 */
replay_results_t replay_aligned_variants(const cpml::model::bpmn_graph& bpmn_graph, const alignments_t& alignments,
                                         const parallel_vertex_pairs<>& parallel_vertices,
                                         const common::execution_context& context);
/**
 * @brief Maps variants back to original traces and creates the table group both with internal joins between and
 * external joins to the activity column.
 *
 * Tables with columns:
 * Alignment: labels and move types - the label for a row is either the name of the activity or the bpmn vertex id of a
 * gateway, along with its type.
 *
 * Association: Has no added columns but has a non-zero number of rows - the individual edges are represented by the
 * join vector between the association and the alignment table and the association and edge class table.
 *
 * Edge Class: Each row in this table represents a separate edge component.
 *
 * @param alignments per variant alignment as produced by @see align_model
 * @param replay_results per variant replay results including relative timestamps as produced by @see
 * replay_aligned_variant
 * @param mapping petri net id to bpmn vertex id
 * @param bpmn_to_string bpmn vertex id to string e.g. start vertex with id 0 ([0 START]) --> "0 START"
 * @param variants the variant trace cache
 * @param activity_column The activity column of the original eventlog
 * @param case_id_column case_id column of the original eventlog
 * @param activity_to_case_join activity table to case table join - since the variants are at the case table level
 * @param model_cache_key bpmn model cache key
 * @param scope
 * @param context
 * @return table_group_stub The table group containing the alignment table, the association table and the edge class
 * table
 */
memory::table_group_t create_tables(const alignments_t& alignments, const replay_results_t& replay_results,
                                    const bpmn::bpmn_to_string_t& bpmn_to_string, const variants& variants,
                                    const memory::column_t& activity_column, const memory::column_t& case_id_column,
                                    const memory::join_projection_vector_t& activity_to_case_join,
#ifdef CELOSTAR
                                    const common::execution_context& context, size_t grain_size);
#else
                                    const cube::registration_options& options, const common::execution_context& context,
                                    size_t grain_size, cube::input_dependencies& dependencies,
                                    const operator_input_columns_t& input_columns);
#endif

}  // namespace celonis::accelerator::operators::process::align_model
