#pragma once

#include "exprs/function_helper.h"

namespace starrocks {
    template<LogicalType LT>
    class CelonisArrayTrimmedMean {
    public:
        /**
          * @param: [input_array, lower cutoff, upper cutoff]
          * @paramType [ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE, BIGINT, BIGINT]
          * @return: DOUBLE
          * This method removes the cutoff of each group and calculates the mean of the remaining values.
          * Null values are ignored. If a group contains only Null then Null is returned for that group.
          * If a group is empty, 0 is returned for this group.
          * For more information check docs https://docs.celonis.com/en/trimmed_mean.html and https://docs.celonis.com/en/pu_trimmed_mean.html
         */
        DEFINE_VECTORIZED_FN(celonis_array_trimmed_mean);
    };
}
