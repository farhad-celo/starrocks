#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisTransitsInterleaved {
public:
    /**
     * @param: [left_primary_keys, left_timestamps, left_sortings, right_primary_keys, right_timestamps, right_sortings, first_last_only]
     * @paramType columns: [ANY_STRUCT, ARRAY_DATETIME, ANY_ARRAY, ANY_STRUCT, ARRAY_DATETIME, ANY_ARRAY, BOOLEAN]
     * @return: ANY_STRUCT
     * left/right_primary_keys is struct-of-array-of-any.
     * This function assumes that left/right_primary_keys are sorted based on left/right_timestamps and
     * left/right_sortings.
     * Both left_sortings and right_sortings must either be set or not set (i.e., set to NULL); you cannot set only one.
     * This function is used to support PQL TRANSIT_COLUMN with INTERLEAVED or NONINTERLEAVED transit type.
     */
    DEFINE_VECTORIZED_FN(transits_interleaved);
};

} // namespace starrocks