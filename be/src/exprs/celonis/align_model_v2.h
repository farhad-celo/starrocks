#pragma once

#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"
#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisAlignModelV2 {
public:
    /**
     * @param: [activityArray, json_bpmn_model_description]
     * @paramType: [ARRAY_VARCHAR, VARCHAR]
     * @return: STRUCT {
     *      variant: ARRAY_VARCHAR (matches activityArray)
     *      alignment_model_vertex_id: ARRAY_BIGINT
     *      alignment_vertex_label: ARRAY_VARCHAR
     *      alignment_move_type: ARRAY_VARCHAR
     *      alignment_activity_index: ARRAY_BIGINT
     *      association_edge_class: ARRAY_BIGINT
     *      association_alignment_index: ARRAY_BIGINT
     *      edge_class_id:  ARRAY_BIGINT
     *      edge_class_type: ARRAY_VARCHAR
     *      alignment_deviation_category: ARRAY_VARCHAR
     *    }
     */
    DEFINE_VECTORIZED_FN(align_model_v2);

    static Status align_model_v2_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status align_model_v2_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

} // namespace starrocks