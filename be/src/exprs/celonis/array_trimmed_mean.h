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
          * This function removes the cutoff of the input array and calculates the mean of the remaining values.
          * NULL values are ignored. If an array contains only NULL then return NULL.
          * If an array is empty, 0 is returned for this array.
          * If the array is NULL, return NULL.
          * If lower/upper_cutoff is NULL, return NULL.
          * For more information check docs https://docs.celonis.com/en/trimmed_mean.html and https://docs.celonis.com/en/pu_trimmed_mean.html
         */
        DEFINE_VECTORIZED_FN(celonis_array_trimmed_mean);
    };
}
