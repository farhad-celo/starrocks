#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType KeyType, LogicalType ValueType>
class CelonisArrayDeduplicateByKey {
public:
    /**
     * @param: [key_array, value_array]
     * @paramType: columns: [ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_VARCHAR | ARRAY_DATETIME]
     * @return: value_array type
     *
     * Processes ordered key-value pairs (encoded in separate key- and value arrays) and constructs the ordered
     * collection of values corresponding to the first occurrence of each unique key.
     *
     * Example:
     *      key array   = [1, 2, 1, 3]
     *      value array = [a, b, c, d]
     *
     *      this encodes the following kv pairs: 1:a, 2:b, 1:c, 3:d
     *
     *      result      = [a, b, d]
     *      value 'c' is discarded because for key '1' we retain the value corresponding to the first occurrence
     *
     * NULL Handling:
     *      If either key- or value array is NULL then the result is also NULL. If a key inside the key array is NULL
     *      the corresponding key-value pair is ignored. NULL values in the value array do contribute to the result.
     *
     * For each input row the key- and value arrays should have the same size, otherwise an error is emitted. Note that
     * the key- and value arrays are allowed to be different types.
     */
    DEFINE_VECTORIZED_FN(array_deduplicate_by_key);
};

} // namespace starrocks
