#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisMathFunctions {
public:
    /**
     * @param: [input_value]
     * @paramType columns: [INT | BIGINT | DOUBLE]
     * @return: input type
     */
    DEFINE_VECTORIZED_FN(square);
};

} // namespace starrocks