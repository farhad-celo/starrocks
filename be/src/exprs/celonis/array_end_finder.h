#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisArrayEndFinder {
public:
    /**
     * @param: [input_array]
     * @paramType columns: [ARRAY_VARCHAR | ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_DATETIME]
     * @return: The first non-NULL value encountered in the array. The return type matches the element type of the input
     * array. If the input array is empty or contains only NULL values, returns NULL.
     */
    DEFINE_VECTORIZED_FN(array_first);

    /**
     * @param: [input_array]
     * @paramType columns: [ARRAY_VARCHAR | ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_DATETIME]
     * @return: The last non-NULL value encountered in the array. The return type matches the element type of the input
     * array. If the input array is empty or contains only NULL values, returns NULL.
     */
    DEFINE_VECTORIZED_FN(array_last);
};

} // namespace starrocks