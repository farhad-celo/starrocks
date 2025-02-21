#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisTrim {
public:
    /**
     *  @param [input_column , trim_characters]
     *  @paramType STRING, STRING
     *  @return STRING
     *  Supports PQL LTRIM: https://docs.celonis.com/en/ltrim.html
     */
    DEFINE_VECTORIZED_FN(ltrim);

    /**
     * @param [input_column , trim_characters]
     * @paramType STRING, STRING
     * @return STRING
     * Supports PQL RTRIM: https://docs.celonis.com/en/rtrim.html
     */
    DEFINE_VECTORIZED_FN(rtrim);

    static Status ltrim_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status rtrim_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status trim_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

} // namespace starrocks