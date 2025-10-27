#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisPeekMergedSortedArrays {
public:
    /**
     * @param: [input_array, timestamp_array, size_array, priority_array, secondary_order_array]
     * @paramType: [ARRAY_VARCHAR | ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_DATETIME, ARRAY_TIMESTAMP, ARRAY_INT, ARRAY_INT, ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_VARCHAR]
     * @return: Same as the element type of the input array.
     * input_array and timestamp_array should have the same number of elements which is equal to the sum of size_array.
     * size_array and priority_array should have the same number of elements.
     * timestamp_array, size_array and priority_array should not be NULL and should not have NULL elements.
     * secondary_order_array column can be a NULL literal, when it is not, secondary_order_array has the same length as
     * the corresponding input_array.
     * This function is equivalent to celonis_array_first(celonis_merge_sorted_arrays(...)).
     * Note that celonis_array_first ignores leading NULLs in the input array.
     */
    DEFINE_VECTORIZED_FN(peek_merged_sorted_arrays);
};

} // namespace starrocks