#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisArrayCountDistinct {
public:
    /**
     * @param: [input_array]
     * @paramType columns: [ARRAY_VARCHAR | ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_DATETIME]
     * @return: BIGINT
     */
    DEFINE_VECTORIZED_FN(array_count_distinct);
};

} // namespace starrocks