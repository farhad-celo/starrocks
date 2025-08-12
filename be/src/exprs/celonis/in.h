#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisIn {
public:
    /**
     * @param: [input_value, match_value_array]
     * @paramType columns: [VARCHAR | INT | BIGINT | DOUBLE | DATETIME, ARRAY of input_value type]
     * @return: BooleanColumn
     * Supports PQL IN https://docs.celonis.com/en/in.html
     * A match value can also be NULL. A NULL value matches with a NULL value in the match array.
     */
    DEFINE_VECTORIZED_FN(in);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(in_constant_match);
    DEFINE_VECTORIZED_FN(in_non_constant_match);
};

} // namespace starrocks