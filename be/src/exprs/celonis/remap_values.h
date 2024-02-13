#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template<LogicalType LT>
class CelonisRemapValues {
public:
    /**
     * @param: [input_value, old_value_array, new_value_array, default_value (optional)]
     * @paramType columns: [VARCHAR | BIGINT | DOUBLE | DATETIME, ARRAY of input_value type, ARRAY of input_value type, input_value type]
     * @return: input_value type column
     * Supports PQL REMAP_VALUES: https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245301/REMAP+VALUES
     * REMAP_INTS: https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11247859/REMAP+INTS
     */
    DEFINE_VECTORIZED_FN(remap_values);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(remap_values_constant_value_map);

    DEFINE_VECTORIZED_FN(remap_values_non_constant_value_map);
};

} // namespace starrocks