#pragma once

#include "exprs/celonis/utils/proto_utils.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_group.h"
#include "modules/operators/process/align_model/align_model.h"
#include "modules/operators/process/align_model/align_model_table_group_node_settings.h"
#include "modules/operators/process/align_model/deviation_category.h"
#include "modules/operators/process/bpmn/bpmn_from_proto.h"

namespace celonis::accelerator {

class TableGroupNode_AlignModelTableGroupNode;
class BpmnModelDescription;

namespace operators::process::align_model {

class create_align_model_tables {
 public:
  create_align_model_tables(memory::column_t activity_column, memory::column_t case_column, memory::table_t case_table,
                            memory::join_projection_vector_t activity_to_case_join,
                            cube::variant_trace_cache_manager& variant_trace_cache_manager,
                            starrocks::celonis::bpmn_model_description model_description,
                            align_model_table_group_node_settings settings)
      : activity_column_{std::move(activity_column)},
        case_column_{std::move(case_column)},
        case_table_{std::move(case_table)},
        activity_to_case_join_{std::move(activity_to_case_join)},
        variant_trace_cache_manager_{variant_trace_cache_manager},
        model_description_{std::move(model_description)},
        settings_{std::move(settings)} {}

  [[nodiscard]] memory::table_group_t operator()(const common::execution_context& context);

 private:
  memory::column_t activity_column_;
  memory::column_t case_column_;
  memory::table_t case_table_;
  memory::join_projection_vector_t activity_to_case_join_;
  cube::variant_trace_cache_manager& variant_trace_cache_manager_;
  const starrocks::celonis::bpmn_model_description model_description_;
  align_model_table_group_node_settings settings_;
};

}  // namespace operators::process::align_model

}  // namespace celonis::accelerator
