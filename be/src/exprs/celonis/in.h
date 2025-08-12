#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisIn {
public:
    /**
     * @param: [input_value, match_value_array]
     * @paramType columns: [VARCHAR | INT | BIGINT | DOUBLE | DATETIME, ARRAY of input_value type]
     * @return: BooleanColumn
     * A match value can also be NULL. A NULL value matches with a NULL value in the match array.
     */
    DEFINE_VECTORIZED_FN(celonis_in);

private:
    template <LogicalType Type>
    static ColumnPtr _celonis_in_impl(ColumnPtr input_column, const DatumArray& match_array);
};

} // namespace starrocks