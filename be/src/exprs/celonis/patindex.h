#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisPatindex {
public:
    /**
     * @param: [input_string, pattern, occurrence (optional)]
     * @paramType columns: [VARCHAR, VARCHAR, BIGINT]
     * @return: BIGINT
     * Supports PQL PATINDEX: https://docs.celonis.com/en/patindex.html
     */
    DEFINE_VECTORIZED_FN(patindex);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(patindex_constant_pattern);

    DEFINE_VECTORIZED_FN(patindex_non_constant_pattern);
};

} // namespace starrocks