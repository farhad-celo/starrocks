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
     * Supports PQL REMAP_VALUES: https://docs.celonis.com/en/remap_values.html
     * REMAP_INTS: https://docs.celonis.com/en/remap_ints.html
     */
    DEFINE_VECTORIZED_FN(remap_values);

    // This function provides a specialized version of remap_values.
    // It treats old_value_array and new_value_array columns as constant.
    DEFINE_VECTORIZED_FN(remap_values_const);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status prepare_const(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(remap_values_constant_value_map);

    DEFINE_VECTORIZED_FN(remap_values_non_constant_value_map);

    DEFINE_VECTORIZED_FN(remap_values_const_non_constant_value_map);
};

} // namespace starrocks