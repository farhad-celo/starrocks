#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisEncodeVariant {
public:

    /**
     * @param: [variant, activity_array]
     * @paramType columns: [ARRAY_VARCHAR, ARRAY_VARCHAR]
     * @return: ARRAY_INT
     * This function expects activity_array column is constant (it also handles the case that activity_array column is
     * not constant) and uses it to create an activity to index (int) map which is used to encode the variants.
     * Before computing the map, we first dedup the activity_array and remove the NULLs.
     * For example activity_array = ['A', 'B', NULL, 'D', 'A'], deduped_activity_array = ['A', 'B', 'D']
     * activity_to_index = {activity: idx for idx, activity in enumerate(deduped_activity_array)}
     * During encoding the variants, NULL activities are ignored. If an activity does not exist in the activity map, it
     * is mapped to -1. For example variant = ['B', 'A', NULL, 'A', 'W'], activity_array = ['A', 'B', NULL, 'D', 'A']
     * encoded_variant = [1, 0, 0, -1].
     * If variant = NULL, encoded_variant = NULL;
     * If activity_array = NULL, encoded_variant = NULL;
     */
    DEFINE_VECTORIZED_FN(encode_variant);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(encode_variant_constant_map);

    DEFINE_VECTORIZED_FN(encode_variant_non_constant_map);
};

} // namespace starrocks