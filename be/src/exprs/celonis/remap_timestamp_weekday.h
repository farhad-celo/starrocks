#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisRemapTimestampWeekday {
public:
    /**
     * @param: [timestamp]
     * @paramType: [DATETIME]
     * @return: BIGINT
     * Returns number of workdays since unix epoch of the given timestamp.
     */
    DEFINE_VECTORIZED_FN(celonis_remap_timestamp_weekday);
};

} // namespace starrocks
