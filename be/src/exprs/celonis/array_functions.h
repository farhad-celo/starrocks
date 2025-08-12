#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisArrayFunctions {
public:
    DEFINE_VECTORIZED_FN(array_is_sorted);

    /**
     * @param: [input_array, timestamp_array, size_array, priority_array]
     * @paramType columns: [ANY_ARRAY, ARRAY_TIMESTAMP, ARRAY_INT, ARRAY_INT]
     * @return: input_arrays type
     * input_array and timestamp_array should have the same number of elements which is equal to the sum of size_array.
     * size_array and priority_array should have the same number of elements.
     * timestamp_array, size_array and priority_array should not be NULL and should not have NULL elements.
     */
    DEFINE_VECTORIZED_FN(merge_sorted_arrays);

    /**
     * @param: [input_array, key_array]
     * @paramType columns: [ANY_ARRAY, ARRAY_VARCHAR]
     * @return: input_array type
     * Returns first elements of input_array that correspond to unique elements of key_array.
     * input_array and key_array should have the same number of elements.
     * key_array should not be NULL and should not have NULL elements.
     */
    DEFINE_VECTORIZED_FN(dedup_sorted_by);

    /**
     * @param: [input_array, offset]
     * @paramType columns: [ANY_ARRAY, BIGINT]
     * @return: input_array type
     * Returns the element that precedes the current element by offset number of elements.
     * The lagging value for a NULL value is the same value as the lagging value of the next non-NULL value.
     * The offset parameter counts only non-NULL values.
     */
    DEFINE_VECTORIZED_FN(array_lag);

    /**
     * @param: [input_array, offset]
     * @paramType columns: [ANY_ARRAY, BIGINT]
     * @return: input_array type
     * Returns the element that follows the current element by offset number of elements.
     * The leading value for a NULL value is the same value as the leading value of the last non-NULL value.
     * The offset parameter counts only non-NULL values.
     */
    DEFINE_VECTORIZED_FN(array_lead);

    /**
     * @param: [input_array]
     * @paramType columns: [ANY_ARRAY]
     * @return: input_array type
     * Returns a new column based on the input column where NULL elements are replaced with empty arrays.
     */
    DEFINE_VECTORIZED_FN(null_to_empty);
};

} // namespace starrocks