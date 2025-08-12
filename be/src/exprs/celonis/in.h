#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisIn {
public:
    /**
     * @param: [input_value, match_value_array]
     * @paramType columns: [VARCHAR | INT | BIGINT | DOUBLE | DATETIME, ARRAY of input_value type]
     * @return: BooleanColumn
     * Supports PQL IN https://docs.celonis.com/en/in.html
     * A match value can also be NULL. A NULL value matches with a NULL value in the match array.
     */
    DEFINE_VECTORIZED_FN(celonis_in);

private:
    template <LogicalType Type>
    static ColumnPtr celonis_in_constant_match(const Columns& columns);

    template <LogicalType Type>
    static ColumnPtr celonis_in_non_constant_match(const Columns& columns);

    template <LogicalType Type>
    static ColumnPtr celonis_in_impl(const Columns& columns);
};

} // namespace starrocks