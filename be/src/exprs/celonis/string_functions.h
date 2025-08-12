#pragma once

#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisStringFunctions {
public:
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

    static Status translate_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status translate_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

}  // namespace starrocks
