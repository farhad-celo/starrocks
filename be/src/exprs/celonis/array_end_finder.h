#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template<LogicalType LT>
class CelonisArrayEndFinder {
public:

    /**
     * @param: [input_array]
     * @paramType columns: [ARRAY_VARCHAR | ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_DATETIME]
     * @return: Same as the element type of the input array.
     */
    DEFINE_VECTORIZED_FN(array_first);
};

} // namespace starrocks