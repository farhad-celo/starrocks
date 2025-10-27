#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisIndexActivity {
public:
    /**
     * @param: [input_array, mode, direction]
     * @paramType columns: [Any ARRAY, VARCHAR, VARCHAR]
     * @return: input_array type
     * mode : "INDEX_ACTIVITY_ORDER" | "INDEX_ACTIVITY_LOOP" | "INDEX_ACTIVITY_TYPE"
     * direction : "FORWARD" | "REVERSE"
     * Supports PQL Process Index operators https://confluence.celonis.com/display/PQLdevelopment/Process+Index
     */
    DEFINE_VECTORIZED_FN(celonis_index_activity);

    static Status celonis_index_activity_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status celonis_index_activity_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

} // namespace starrocks
