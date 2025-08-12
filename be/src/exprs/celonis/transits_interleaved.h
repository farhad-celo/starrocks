#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisTransitsInterleaved {
public:
    /**
     * @param: [left_primary_keys, left_timestamps, right_primary_keys, right_timestamps, first_last_only]
     * @paramType columns: [ANY_STRUCT, ARRAY_DATETIME, ANY_STRUCT, ARRAY_DATETIME, BOOLEAN]
     * @return: ANY_STRUCT
     * left/right_primary_keys is struct-of-array-of-any.
     * This function is used to support PQL TRANSIT_COLUMN
     */
    DEFINE_VECTORIZED_FN(transits_interleaved);
};

} // namespace starrocks