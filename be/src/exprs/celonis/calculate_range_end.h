#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisCalculateRangeEnd {
public:
    /**
     * @param: [start, step_size, step_count]
     * @paramType columns: [DATETIME, VARCHAR, BIGINT]
     * @return: DATETIME
     * step_size is a string of the form '<number><unit>' (e.g., '1M'), where <number> is the number of units and <unit>
     * is one of the following
     * 1. h: hour
     * 2. D: day
     * 3. M: month
     * 4. Y: year
     * The return value end = start + step_size * step_count
     */
    DEFINE_VECTORIZED_FN(calculate_range_end);
};

} // namespace starrocks