#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisDecodeString {
public:
    /**
     * @param: [dict_id, dict_array]
     * @paramType columns: [INT, ARRAY_VARCHAR]
     * @return: VARCHAR
     * 
     * This functions decodes a dictionary id back into string space using a dictionary (reverses
     * CELONIS_ENCODE_STRING). The dictionary is computed from the dict_array by removing duplicates and NULLs.
     * For example dict_array = ['A', 'B', NULL, 'D', 'A'] becomes deduped_dict_array = ['A', 'B', 'D']
     * string_to_index = {string: idx for idx, string in enumerate(deduped_dict_array)}
     * index_to_string = {id: string for string, id in string_to_index}
     * If the dict_id does not exist in the dictionary, NULL is returned.
     * The function returns NULL if either the dict_id or the dict_array is NULL.
     */
    DEFINE_VECTORIZED_FN(decode_string);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(decode_string_constant_map);

    DEFINE_VECTORIZED_FN(decode_string_non_constant_map);
};

} // namespace starrocks