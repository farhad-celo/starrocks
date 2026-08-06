#pragma once

#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"
#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisCreateAlignment {
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
     *      alignment_deviation_category: ARRAY_VARCHAR
     *      alignment_deviation_category_v2: ARRAY_VARCHAR
     *  
     *      for <EDGE_TYPE> in [SYNC_EDGE, MODEL_EDGE, LOG_EDGE, SKIP_EDGE, UNMAPPED_EDGE, MISSING_VIOLATION, EXCLUSIVE_VIOLATION, INCOMPLETE_VIOLATION]:    
     *          <EDGE_TYPE>_model_vertex_id: ARRAY_BIGINT
     *          <EDGE_TYPE>_vertex_label: ARRAY_VARCHAR
     *          <EDGE_TYPE>_move_type: ARRAY_VARCHAR
     *          <EDGE_TYPE>_deviation_category: ARRAY_VARCHAR
     *          <EDGE_TYPE>_edge_class: ARRAY_BIGINT
     *          <EDGE_TYPE>_alignment_index: ARRAY_BIGINT
     *    }
     */
    DEFINE_VECTORIZED_FN(create_alignment);

    static Status create_alignment_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status create_alignment_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

} // namespace starrocks
