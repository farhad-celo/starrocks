#pragma once

#include <string_view>
#include <utility>

#include "align_model.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context_fwd.h"
#ifndef CELOSTAR
#include "modules/cube/query_scope.h"
#include "modules/cube/query_scope_fwd.h"
#include "modules/cube/table_registry/table_registry.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/table_group.h"
#include "modules/operators/framework/table_group_node.h"
#endif

namespace celonis::accelerator {

class TableGroupNode_AlignModelTableGroupNode;

namespace operators::process::align_model {

#ifndef CELOSTAR
class align_model_table_group_node final : public framework::table_group_node {
 public:
  align_model_table_group_node(cube::query_scope& scope, const TableGroupNode_AlignModelTableGroupNode& node);

  [[nodiscard]] memory::table_group_t do_execute(cube::execution::tracking::operator_tracker& tracker,
                                                 cube::operator_executor& executor,
                                                 common::execution_context& operator_context) const override;

  [[nodiscard]] static constexpr std::string_view get_user_visible_operator_name() noexcept { return "ALIGN_MODEL"; }

  /**
   * @brief Creates the cache key for the ALIGN_MODEL cache. Take care to hash this if used for swapping as the string
   * can grow large since it includes the model representation.
   *
   * @param activity_column The activity column of the ALIGN_MODEL operator
   * @param model The model provided to the ALIGN_MODEL operator
   * @return std::string (unhashed) cache_key
   */
  [[nodiscard]] static std::string make_table_group_cache_key(const memory::column_t& activity_column,
                                                              const BpmnModelDescription& model,
                                                              const common::execution_context& context);
  /**
   * @brief Creates the cache key for the pruned variants cache. We use the ordered set of activities since models with
   * the same set of activities in the model produce the same pruned variants. Take care to hash this if used for
   * swapping as the string can grow large since it includes the activity names.
   *
   * @param activity_column The activity column of the ALIGN_MODEL operator
   * @param model The model provided to the ALIGN_MODEL operator
   * @return std::string (unhashed) cache_key
   */
  [[nodiscard]] static std::string make_pruned_variant_cache_key(const memory::column_t& activity_column,
                                                                 const BpmnModelDescription& modelconst,
                                                                 const common::execution_context& context);

 private:
  cube::query_scope& scope_;
  const TableGroupNode_AlignModelTableGroupNode& node_;
  cube::table_registry& registry_;
};
#endif

class create_align_model_tables {
#ifdef CELOSTAR
public:
  create_align_model_tables(memory::column_t activity_column, memory::column_t case_column, memory::table_t case_table,
                            memory::join_projection_vector_t activity_to_case_join,
                            cube::variant_trace_cache_manager* variant_trace_cache_manager,
                            const BpmnModelDescription& model_description)
      : activity_column_{std::move(activity_column)},
        case_column_{std::move(case_column)},
        case_table_{std::move(case_table)},
        activity_to_case_join_{std::move(activity_to_case_join)},
        variant_trace_cache_manager_{variant_trace_cache_manager},
        model_description_{model_description} {}

  [[nodiscard]] memory::table_group_t operate(const common::execution_context& context);

private:
  memory::column_t activity_column_;
  memory::column_t case_column_;
  memory::table_t case_table_;
  memory::join_projection_vector_t activity_to_case_join_;
  cube::variant_trace_cache_manager* variant_trace_cache_manager_;
  const BpmnModelDescription& model_description_;
#else
 public:
  create_align_model_tables(cube::query_scope& scope, memory::column_t activity_column, memory::column_t case_column,
                            const BpmnModelDescription& model_description,
                            cube::execution::tracking::add_telemetry_counter_fn add_telemetry_counter)
      : scope_{scope},
        activity_column_{std::move(activity_column)},
        case_column_{std::move(case_column)},
        model_description_{model_description},
        add_telemetry_counter_{std::move(add_telemetry_counter)} {}

  [[nodiscard]] memory::table_group_t operator()([[maybe_unused]] cube::cube_data_model& data_model,
                                                 const memory::management::swap_info& sinfo,
                                                 const common::execution_context& context);

 private:
  cube::query_scope& scope_;
  memory::column_t activity_column_;
  memory::column_t case_column_;
  const BpmnModelDescription& model_description_;
  cube::execution::tracking::add_telemetry_counter_fn add_telemetry_counter_;
#endif
};

}  // namespace operators::process::align_model

}  // namespace celonis::accelerator
