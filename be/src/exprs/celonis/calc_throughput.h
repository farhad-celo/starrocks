#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType ActivityLT>
class CelonisCalcThroughputFunctions {
public:
    /**
     * @param: [activity_array, timestamp_array, start_activity, end_activity, start_label, end_label]
     * @paramType: [ARRAY_ActivityLT, ARRAY_BIGINT, ActivityLT, ActivityLT, VARCHAR, VARCHAR]
     * @return: BIGINT
     * ActivityLT is INT | BIGINT | VARCHAR.
     * activity_array and timestamp_array should have the same number of elements.
     * Implements PQL CALC_THROUGHPUT https://docs.celonis.com/en/calc_throughput.html
     */
    DEFINE_VECTORIZED_FN(celonis_calc_throughput);
};

} // namespace starrocks
