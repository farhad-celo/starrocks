#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisGreatestLeast {
public:
    /**
     * @param: [column_1, [..., column_n]]
     * @paramType: columns: [VARCHAR | BIGINT | DOUBLE | DATETIME] (column_1 - column_n)
     * @return: column_1 type
     * Supports PQL GREATEST: https://docs.celonis.com/en/greatest.html
     */
    DEFINE_VECTORIZED_FN(celonis_greatest);

    /**
     * @param: [column_1, [..., column_n]]
     * @paramType: columns: [VARCHAR | BIGINT | DOUBLE | DATETIME] (column_1 - column_n)
     * @return: column_1 type
     * Supports PQL LEAST: https://docs.celonis.com/en/least.html
     */
    DEFINE_VECTORIZED_FN(celonis_least);
};

} // namespace starrocks
