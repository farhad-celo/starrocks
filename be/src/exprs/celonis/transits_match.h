#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisTransitsMatch {
public:
    /**
     * @param: [left_primary_keys, left_match, right_primary_keys, right_match, left_manual, right_manual]
     * @paramType columns: [ANY_STRUCT, ANY_ARRAY, ANY_STRUCT, ANY_ARRAY, ANY_ARRAY, ANY_ARRAY]
     * @return: ANY_STRUCT
     * left/right_primary_keys is struct-of-array-of-any.
     * both manual_left and manual_right are optional, if one is set, the other must also be set.
     * This function is used to support PQL TRANSIT_COLUMN
     */
    DEFINE_VECTORIZED_FN(transits_match);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(transits_match_constant_manual);

    DEFINE_VECTORIZED_FN(transits_match_non_constant_manual);
};

} // namespace starrocks