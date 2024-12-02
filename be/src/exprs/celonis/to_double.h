#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template<LogicalType LT>
class CelonisToDouble {
public:
    /**
     * @param: [input]
     * @paramType columns: [DOUBLE | INT | BIGINT | DATETIME]
     * @return: DOUBLE
     * When input column is DATETIME, this function converts it to unix_milliseconds.
     */
    DEFINE_VECTORIZED_FN(to_double);
};

} // namespace starrocks