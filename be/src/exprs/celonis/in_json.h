#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisInJson {
public:
    /**
     * @param: [input_value, match_value_array_json_string]
     * @paramType columns: [VARCHAR | INT | BIGINT | DOUBLE | DATETIME, VARCHAR]
     * @return: BooleanColumn
     * Supports PQL IN https://docs.celonis.com/en/in.html
     * A match value can also be NULL. A NULL value matches with a NULL value in the match array.
     * Match array is passed in as a json string. For example
     * match_value_array_json_string = "["A","B",null]" means match_array = ["A", "B", NULL]
     * match_value_array_json_string must represent a JSON array.
     * The implementation requires a constant match_value_array_json_string column; otherwise,
     * it returns an InvalidArgument status.
     */
    DEFINE_VECTORIZED_FN(in_json);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(in_json_constant_match);
    DEFINE_VECTORIZED_FN(in_json_non_constant_match);
};

} // namespace starrocks