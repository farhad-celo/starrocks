#pragma once

#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisAdjustDailyTimestamps {
public:
    /**
    * Takes as input timestamp arrays with sorting values and day-based flags. Will give two outputs:
    * - The rewritten timestamps based on the rules for rewriting day-based activities
    * - The new positions of the timestamps within their cases based on the rules for reordering day-based activities.
    *
    * The sorting input is optional. If no sorting input is given the function basically returns the identity.
    *
    * @param: [timestamps, is_day_based, sorting (optional)]
    * @paramType: [ARRAY_DATETIME, ARRAY_BOOLEAN, ARRAY_BIGINT]
    * @return: STRUCT {
    *      adjusted_timestamps: ARRAY_DATETIME
    *      reordering: ARRAY_BIGINT
    *    }
    */
    DEFINE_VECTORIZED_FN(celonis_adjust_daily_timestamps);
};

} // namespace starrocks