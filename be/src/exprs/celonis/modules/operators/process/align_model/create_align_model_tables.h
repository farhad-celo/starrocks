#pragma once

#include "exprs/celonis/utils/proto_utils.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_group.h"
#include "modules/operators/process/align_model/align_model.h"
#include "modules/operators/process/align_model/align_model_table_group_node_settings.h"
#include "modules/operators/process/align_model/v2/create_alignment_output_projection.h"

namespace celonis::accelerator {

class TableGroupNode_AlignModelTableGroupNode;
class BpmnModelDescription;

namespace operators::process::align_model {

class create_align_model_tables {
 public:
  // The model is borrowed and must outlive this operator.
  create_align_model_tables(memory::column_t activity_column, memory::column_t case_column, row_id case_table_row_count,
                            memory::join_projection_vector_t activity_to_case_join,
                            const starrocks::celonis::bpmn_model_description& model_description,
                            align_model_table_group_node_settings settings,
                            v2::create_alignment_output_projection output_projection)
      : activity_column_{std::move(activity_column)},
        case_column_{std::move(case_column)},
        case_table_row_count_{case_table_row_count},
        activity_to_case_join_{std::move(activity_to_case_join)},
        model_description_{model_description},
        settings_{std::move(settings)},
        output_projection_{std::move(output_projection)} {}

  [[nodiscard]] memory::table_group_t operator()(const common::execution_context& context);

 private:
  memory::column_t activity_column_;
  memory::column_t case_column_;
  row_id case_table_row_count_;
  memory::join_projection_vector_t activity_to_case_join_;
  const starrocks::celonis::bpmn_model_description& model_description_;
  align_model_table_group_node_settings settings_;
  v2::create_alignment_output_projection output_projection_;
};

}  // namespace operators::process::align_model

}  // namespace celonis::accelerator
