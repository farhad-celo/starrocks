#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisMultiIn {
public:
    /**
     * @param: [input_struct, struct_of_arrays]
     * @paramType columns: [ANY_STRUCT, ANY_STRUCT]
     * @return: BOOLEAN
     */
    DEFINE_VECTORIZED_FN(multi_in);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(multi_in_constant_config);

    DEFINE_VECTORIZED_FN(multi_in_non_constant_config);
};

} // namespace starrocks