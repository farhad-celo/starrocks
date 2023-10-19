#pragma once

#include <string>

#include "modules/common/int_types.h"
#include "modules/cube/event_table_config_fwd.h"
#include "modules/cube/execution/tracking/operator_tracker_fwd.h"
#include "modules/cube/query_scope_fwd.h"
#include "modules/cube/table_registry/cached_table_registry.h"
#include "modules/memory/builders/result_column_builder_fwd.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_fwd.h"
#include "modules/operators/framework/cached_operator.h"
#include "modules/operators/framework/operator_node.h"
#include "modules/operators/process/alignment/log_aligner.h"
#include "modules/operators/process/alignment/log_alignment_result_cache_fwd.h"
#include "modules/operators/process/alignment/log_alignment_result_fwd.h"
#include "modules/query/operators.pb.h"

namespace celonis::accelerator::operators::process::alignment {

namespace petri_net {
struct petri_net_representation;
struct unfolding_representation;
}  // namespace petri_net

struct alignment_input_columns {
  memory::column_t activity_column;
  memory::column_t pruned_activity_column;
  memory::column_t variant_column;
  memory::column_t pruned_variant_column;
};

class alignment_operator final : public cached_operator {
 public:
  alignment_operator(memory::column_t activity_column, memory::column_t pruned_activity_column,
                     memory::column_t variant_column, memory::column_t pruned_variant_column,
                     cube::table_data& common_table_data, memory::join_projection_vector_t projection_vector,
                     const cube::event_table_config* event_config,
                     operators::process::alignment::log_alignment_result_cache& log_alignment_cache,
                     const RLAlignOperatorNode& node, cube::query_scope& scope,
                     common::execution_context& operator_context, log_aligner_config log_aligner_cfg,
                     cube::execution::tracking::add_telemetry_counter_fn add_telemetry_counter);

  [[nodiscard]] const std::string& get_operator_tracking_key() const noexcept override;
  [[nodiscard]] memory::table* get_common_table() override;
  [[nodiscard]] operator_input_columns_t get_input_columns() const override;
  [[nodiscard]] memory::column_processing_state get_result_state() override;
  [[nodiscard]] const std::string& get_cache_key() override;
  [[nodiscard]] memory::warnings_t get_warnings() override { return warnings; }

  [[nodiscard]] memory::builders::result_column_builder_t execute(common::execution_context& context) override;

  inline static const std::string OPERATOR_KEY{"RL_ALIGN"};  // NOLINT(cert-err58-cpp)

 private:
  [[nodiscard]] log_alignment_result_t calculate_log_alignment(const std::string& alignment_table_cache_key,
                                                               common::execution_context& operator_context);

  const memory::column_t activity_column_;
  const memory::column_t pruned_activity_column_;
  const memory::column_t variant_column_;
  const memory::cache::variant_trace_cache_t variant_cache_;
  const memory::column_t pruned_variant_column_;
  const memory::cache::variant_trace_cache_t pruned_variant_cache_;
  cube::table_data& common_table_data_;
  memory::join_projection_vector_t projection_vector_;
  cube::cached_table_registry& table_registry_;
  log_alignment_result_cache& log_alignment_cache_;
  const cube::event_table_config* event_config_;
  const RLAlignOperatorNode& node_;
  log_aligner_config log_aligner_cfg_;
  const cube::query_scope& scope_;
  std::string table_cache_key_;
  memory::warnings_t warnings{std::make_shared<memory::warnings_container_t>()};

  /**
   * The specific column referred by the RLAlignOperatorNode is stored in the align_table under this cache key
   */
  const std::string column_cache_key_;
  cube::execution::tracking::add_telemetry_counter_fn add_telemetry_counter_;
};

class alignment_operator_node final : public framework::operator_node {
 public:
  alignment_operator_node(
      operator_node* activity_column_operator_node, operator_node* pruned_activity_column_operator_node,
      operator_node* variant_column_operator_node, operator_node* pruned_variant_column_operator_node,
      log_alignment_result_cache& log_alignment_cache, const RLAlignOperatorNode& node, cube::query_scope& scope,
      log_aligner_config log_aligner_cfg = log_aligner_config::make_default(),
      std::optional<cube::execution::tracking::add_telemetry_counter_fn> add_telemetry_counter = std::nullopt);

  [[nodiscard]] const std::string& get_cache_key() const override { return node_.metadata().cache_key(); }

  [[nodiscard]] const std::string& get_operator_key() const override { return alignment_operator::OPERATOR_KEY; }

 private:
  memory::column_t do_execute(cube::execution::tracking::operator_tracker& tracker, cube::operator_executor& executor,
                              common::execution_context& operator_context) const override;

  memory::table* do_compute_common_table(cube::execution::tracking::operator_tracker& tracker,
                                         cube::operator_executor& executor,
                                         common::execution_context& operator_context) const override;

  cube::table_data& get_or_compute_common_table_data(const alignment_input_columns& input_cols,
                                                     const cube::event_table_config* event_config) const;

  const cube::event_table_config* get_event_table_config(memory::table* table,
                                                         const common::execution_context& operator_context) const;

  operator_node* activity_column_operator_node_;
  operator_node* pruned_activity_column_operator_node_;
  operator_node* variant_column_operator_node_;
  operator_node* pruned_variant_column_operator_node_;
  log_alignment_result_cache& log_alignment_cache_;
  const RLAlignOperatorNode& node_;
  cube::query_scope& scope_;
  log_aligner_config log_aligner_cfg_;
  std::optional<cube::execution::tracking::add_telemetry_counter_fn> add_telemetry_counter_{};
};

}  // namespace celonis::accelerator::operators::process::alignment
