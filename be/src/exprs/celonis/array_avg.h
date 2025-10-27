#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisArrayAvg {
public:
    /**
     * @param: [input_array]
     * @paramType columns: [ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE]
     * @return: DOUBLE
     */
    DEFINE_VECTORIZED_FN(array_avg);
};

} // namespace starrocks