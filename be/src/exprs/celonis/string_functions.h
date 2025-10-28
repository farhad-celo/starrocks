#pragma once

#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisStringFunctions {
public:
    /**
     * @param: [string_value, ...] or [string_array]
     * @paramType: [VARCHAR, ...] or [ARRAY_VARCHAR]
     * @return LARGEINT
     * celonis_xx_hash3_128(NULL) != NULL
     * celonis_xx_hash3_128(NULL) == celonis_xx_hash3_128(NULL)
     * celonis_xx_hash3_128(NULL) != celonis_xx_hash3_128(NULL, NULL)
     * celonis_xx_hash3_128(NULL array) != celonis_xx_hash3_128([NULL]) (NULL array means the type is array and the array is NULL)
     * celonis_xx_hash3_128([NULL, NULL]) != celonis_xx_hash3_128([NULL])
     */
    DEFINE_VECTORIZED_FN(xx_hash3_128);

    /**
     * @param: [string_value, ...] or [string_array]
     * @paramType: [VARCHAR, ...] or [ARRAY_VARCHAR]
     * @return LARGEINT
     * celonis_xx_hash3_128_v2(NULL) != NULL
     * celonis_xx_hash3_128_v2(NULL) == celonis_xx_hash3_128_v2(NULL)
     * celonis_xx_hash3_128_v2(NULL) != celonis_xx_hash3_128_v2(NULL, NULL)
     * celonis_xx_hash3_128_v2(NULL array) != celonis_xx_hash3_128_v2([NULL]) (NULL array means the type is array and the array is NULL)
     * celonis_xx_hash3_128_v2([NULL, NULL]) != celonis_xx_hash3_128_v2([NULL])
     */
    DEFINE_VECTORIZED_FN(xx_hash3_128_v2);

    /**
     * @param: [string_value, ...] or [string_array]
     * @paramType: [VARCHAR, ...] or [ARRAY_VARCHAR]
     * @return LARGEINT
     * celonis_xx_hash3_128_v3(NULL) != NULL
     * celonis_xx_hash3_128_v3(NULL) == celonis_xx_hash3_128_v3(NULL)
     * celonis_xx_hash3_128_v3(NULL) != celonis_xx_hash3_128_v3(NULL, NULL)
     * celonis_xx_hash3_128_v3(NULL array) != celonis_xx_hash3_128_v3([NULL]) (NULL array means the type is array and the array is NULL)
     * celonis_xx_hash3_128_v3([NULL, NULL]) != celonis_xx_hash3_128_v3([NULL])
     * Unlike celonis_xx_hash3_128_v2, celonis_xx_hash3_128_v3 does not suffer from the "concat" issue.
     * celonis_xx_hash3_128_v3("22", "44") != celonis_xx_hash3_128_v3("2", "244")
     */
    DEFINE_VECTORIZED_FN(xx_hash3_128_v3);

    /**
     * @param: [string_value, ...] or [string_array]
     * @paramType: [VARCHAR, ...] or [ARRAY_VARCHAR]
     * @return LARGEINT
     * v4 preserves the same functional properties as v3 but produces different hash values.
     */
    DEFINE_VECTORIZED_FN(xx_hash3_128_v4);

    /**
     * @param: [string_value, ...] or [string_array]
     * @paramType: [VARCHAR, ...] or [ARRAY_VARCHAR]
     * @return TYPE_VARCHAR
     * celonis_xx_hash3_96(NULL) != NULL
     * celonis_xx_hash3_96(NULL) == celonis_xx_hash3_96(NULL)
     * celonis_xx_hash3_96(NULL) != celonis_xx_hash3_96(NULL, NULL)
     * celonis_xx_hash3_96(NULL array) != celonis_xx_hash3_96([NULL]) (NULL array means the type is array and the array is NULL)
     * celonis_xx_hash3_96([NULL, NULL]) != celonis_xx_hash3_96([NULL])
     */
    DEFINE_VECTORIZED_FN(xx_hash3_96);

    /**
     * @param: [string_value, ...] or [string_array]
     * @paramType: [VARCHAR, ...] or [ARRAY_VARCHAR]
     * @return LARGEINT
     * celonis_xx_hash3_128_nullable(NULL, ...) == NULL
     * celonis_xx_hash3_128_nullable([NULL, ...]) == NULL
     * celonis_xx_hash3_128_nullable(NULL array) == NULL (NULL array means the type is array and the array is NULL)
     */
    DEFINE_VECTORIZED_FN(xx_hash3_128_nullable);

    /**
     * @param: [string_value, pattern_value, replace_value]
     * @paramType: [BinaryColumn, BinaryColumn, BinaryColumn]
     * @return: BinaryColumn
     */
    DEFINE_VECTORIZED_FN(translate);

    /**
     * @param: [string_value]
     * @paramType: [BinaryColumn]
     * @return: BinaryColumn
     *
     * Sanitizes an invalid UTF-8 character sequence by replacing all invalid UTF-8 characters with '?'.
     * Implements format::utf::sanitize_invalid_utf8() of cpm-query-engine.
     */
    DEFINE_VECTORIZED_FN(sanitize_invalid_utf8);

    /**
     * @param: [string_value, delimiter, field]
     * @paramType: [BinaryColumn, BinaryColumn, IntColumn]
     * @return: BinaryColumn
     * Implements PQL STRING_SPLIT https://docs.celonis.com/en/string_split.html
     */
    DEFINE_VECTORIZED_FN(string_split);

    /**
     * @param: [string_value]
     * @paramType: [BinaryColumn]
     * @return: DoubleColumn
     */
    DEFINE_VECTORIZED_FN(string_to_double);

    /**
     * @param: [string_value]
     * @paramType: [BinaryColumn]
     * @return: BigintColumn
     */
    DEFINE_VECTORIZED_FN(string_to_int);

    /**
     * @param: [input_string, match_strings, top_k, separator]
     * @paramType: [VARCHAR, ARRAY_VARCHAR, INT, VARCHAR]
     * @return: VARCHAR
     * Implements PQL MATCH_STRINGS https://docs.celonis.com/en/match_strings.html
     */
    DEFINE_VECTORIZED_FN(match_strings);

    /**
     * @param: [input_string]
     * @paramType: [BinaryColumn]
     * @return: BinaryColumn
     * Implements PQL UPPER https://docs.celonis.com/en/upper.html
     */
    DEFINE_VECTORIZED_FN(upper);

    /**
     * @param: [input_string]
     * @paramType: [BinaryColumn]
     * @return: BinaryColumn
     * Implements PQL LOWER https://docs.celonis.com/en/lower.html
     */
    DEFINE_VECTORIZED_FN(lower);

    static Status translate_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status translate_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status match_strings_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status match_strings_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(in_like_constant_patterns);
    DEFINE_VECTORIZED_FN(in_like_non_constant_patterns);

    DEFINE_VECTORIZED_FN(match_strings_constant);
    DEFINE_VECTORIZED_FN(match_strings_non_constant);
};

} // namespace starrocks
