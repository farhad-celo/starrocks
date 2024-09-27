#pragma once

#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisStringFunctions {
public:
    /**
     * @param: [string_value, ...]
     * @paramType: [BinaryColumn, ...]
     * @return LargeIntColumn
     */
    DEFINE_VECTORIZED_FN(xx_hash3_128);

    /**
     * @param: [string_value, ...]
     * @paramType: [BinaryColumn, ...]
     * @return LargeIntColumn
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
     * @param: [input_string, patterns]
     * @paramType: [VARCHAR, ARRAY_VARCHAR]
     * @return: BIGINT
     * Implements PQL IN_LIKE https://docs.celonis.com/en/in_like.html
     */
    DEFINE_VECTORIZED_FN(in_like);

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

    static Status in_like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status in_like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status match_strings_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status match_strings_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(in_like_constant_patterns);
    DEFINE_VECTORIZED_FN(in_like_non_constant_patterns);

    DEFINE_VECTORIZED_FN(match_strings_constant);
    DEFINE_VECTORIZED_FN(match_strings_non_constant);
};

}  // namespace starrocks
