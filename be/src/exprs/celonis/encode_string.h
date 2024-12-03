#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisEncodeString {
public:
    /**
     * @param: [string, dict_array]
     * @paramType columns: [VARCHAR, ARRAY_VARCHAR]
     * @return: INT
     * 
     * This functions encodes a string into integer space using a dictionary. The dictionary is computed from
     * the dict_array by removing duplicates and NULLs.
     * For example dict_array = ['A', 'B', NULL, 'D', 'A'] becomes deduped_dict_array = ['A', 'B', 'D']
     * The dictionary ids are computed by:
     * string_to_index = {string: idx for idx, string in enumerate(deduped_dict_array)}
     * If the string does not exist in the dictionary, -1 is returned.
     * The function returns NULL if either the string or the dict_array is NULL.
     */
    DEFINE_VECTORIZED_FN(encode_string);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(encode_string_constant_map);

    DEFINE_VECTORIZED_FN(encode_string_non_constant_map);
};

} // namespace starrocks