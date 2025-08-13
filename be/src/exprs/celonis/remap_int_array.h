#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisRemapIntArray {
public:
    /**
     * @param: [input_array, old_value_array, new_value_array, default_value (optional)]
     * @paramType columns: [ARRAY_BIGINT, ARRAY_BIGINT, ARRAY_BIGINT, BIGINT]
     * @return: ARRAY_BIGINT column
     * Maps each element in input_array using the mapping defined by old_value_array and new_value_array.
     * For each element in input_array, if it matches an element in old_value_array at index i,
     * it gets replaced by the element in new_value_array at index i.
     * If no match is found and default_value is provided, uses default_value.
     * If no match is found and no default_value, keeps the original value.
     */
    DEFINE_VECTORIZED_FN(remap_int_array);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(remap_int_array_constant_value_map);

    DEFINE_VECTORIZED_FN(remap_int_array_non_constant_value_map);
};

} // namespace starrocks
