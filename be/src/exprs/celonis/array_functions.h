#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisArrayFunctions {
public:
    /**
    * @param: [input_array, timestamp_array, size_array, priority_array, secondary_order_array, limit]
    * @paramType: [ANY_ARRAY, ARRAY_TIMESTAMP, ARRAY_INT, ARRAY_INT, ARRAY_INT | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_STRING, BIGINT]
    * @return: input_array type
    * input_array and timestamp_array should have the same number of elements which is equal to the sum of size_array.
    * size_array and priority_array should not be NULL and should not have NULL elements.
    * If limit is set and it is not NULL, only output the first limit elements.
    * If limit is set, priority_array can be a NULL literal and if it is set, it should have the same number of
    * elements as input/timestamp_array. Otherwise priority_array should have the same number of elements as size_array.
    * secondary_order_array column can be a NULL literal, when it is not, secondary_order_array has the same length as
    * the corresponding input_array.
    * NULL values are placed at the first during ordering.
    */
    DEFINE_VECTORIZED_FN(merge_sorted_arrays);

    /**
     * @param: [input_array, key_array]
     * @paramType: [ANY_ARRAY, ARRAY_VARCHAR]
     * @return: input_array type
     * Returns first elements of input_array that correspond to unique elements of key_array.
     * input_array and key_array should have the same number of elements.
     * key_array should not be NULL and should not have NULL elements.
     */
    DEFINE_VECTORIZED_FN(dedup_sorted_by);

    /**
     * @param: [input_array, offset]
     * @paramType: [ANY_ARRAY, BIGINT]
     * @return: input_array type
     * Returns the element that precedes the current element by offset number of elements.
     * The lagging value for a NULL value is the same value as the lagging value of the next non-NULL value.
     * The offset parameter counts only non-NULL values.
     */
    DEFINE_VECTORIZED_FN(array_lag);

    /**
     * @param: [input_array, offset]
     * @paramType: [ANY_ARRAY, BIGINT]
     * @return: input_array type
     * Returns the element that follows the current element by offset number of elements.
     * The leading value for a NULL value is the same value as the leading value of the last non-NULL value.
     * The offset parameter counts only non-NULL values.
     */
    DEFINE_VECTORIZED_FN(array_lead);

    /**
     * @param: [input_array]
     * @paramType: [ANY_ARRAY]
     * @return: input_array type
     * Returns a new column based on the input column where NULL elements are replaced with empty arrays.
     */
    DEFINE_VECTORIZED_FN(null_to_empty);

    /**
     * @param: [activities, begin_range_activity, begin_range_mode, end_range_activity, end_range_mode]
     * @paramType: [ARRAY_VARCHAR, VARCHAR, VARCHAR, VARCHAR, VARCHAR]
     * @return: ARRAY_BIGINT
     * Implements PQL CALC_CROP: https://docs.celonis.com/en/calc_crop.html
     */
    DEFINE_VECTORIZED_FN(calc_crop);

    /**
     * @param: [activities, begin_range_activity, begin_range_mode, end_range_activity, end_range_mode]
     * @paramType: [ARRAY_VARCHAR, VARCHAR, VARCHAR, VARCHAR, VARCHAR]
     * @return: ARRAY_VARCHAR
     * Implements PQL CALC_CROP_TO_NULL: https://docs.celonis.com/en/calc_crop_to_null.html
     */
    DEFINE_VECTORIZED_FN(calc_crop_to_null);

    /**
     * @param: [input_array]
     * @paramType: [ARRAY_VARCHAR | ARRAY_BIGINT | ARRAY_DOUBLE | ARRAY_DATETIME]
     * @return: BIGINT
     * It counts number of non-NULL elements in the array
     */
    DEFINE_VECTORIZED_FN(array_count);

    /**
     * @param: [bool_array]
     * @paramType: [ARRAY_BOOLEAN]
     * @return: BOOLEAN
     * It replaces SELECT COUNT(CASE WHEN BOOLEAN_COL THEN 1 ELSE NULL) > 0
     */
    DEFINE_VECTORIZED_FN(array_bool_or);
};

} // namespace starrocks