#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisMultiIn {
public:
    /**
     * Multi-column IN predicate that checks if a tuple of input columns matches any tuple in a list of match tuples.
     *
     * @param input_struct: A struct containing the input columns to check
     *   - Structure: ROW(col1, col2, ..., colN)
     *   - Example: ROW(customer_id, product_id, year)
     *   - Each field can be any type (numeric, string, date, etc.)
     *
     * @param struct_of_arrays: A struct containing arrays of match values, one array per field
     *   - Structure: ROW([match1_col1, match2_col1, ...], [match1_col2, match2_col2, ...], ...)
     *   - Example: ROW([100, 200, 300], ['prod_a', 'prod_b', 'prod_c'], [2023, 2024, 2025])
     *   - This represents match tuples: (100, 'prod_a', 2023), (200, 'prod_b', 2024), (300, 'prod_c', 2025)
     *   - **Important**: All arrays MUST have the same length, otherwise returns FALSE
     *   - **Important**: This parameter must be constant (cannot be a column expression)
     *
     * The function transposes the struct_of_arrays from column-wise arrays to row-wise tuples, then checks
     * if the input_struct matches any of these tuples.
     *
     * @paramType columns: [ANY_STRUCT, ANY_STRUCT]
     * @return: BOOLEAN
     *   - TRUE: if the input tuple matches at least one match tuple
     *   - FALSE: if no match is found, or if arrays have inconsistent lengths
     *   - NULL: if either input_struct or struct_of_arrays is NULL
     *
     * Example usage:
     *   CELONIS_MULTI_IN(
     *     ROW(customer_id, product_type),
     *     ROW([100, 200, 300], ['A', 'B', 'C'])
     *   )
     *   Returns TRUE if (customer_id, product_type) equals (100, 'A'), (200, 'B'), or (300, 'C')
     */
    DEFINE_VECTORIZED_FN(multi_in);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(multi_in_constant_config);

    DEFINE_VECTORIZED_FN(multi_in_non_constant_config);
};

} // namespace starrocks