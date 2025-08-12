#pragma once

#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisLike {
public:
    /**
     * @param: [string_value, pattern]
     * @paramType: [BinaryColumn, BinaryColumn]
     * @return: BooleanColumn
     * Supports PQL LIKE https://docs.celonis.com/en/like.html
     * Not supported
     * - pattern supports const value only.
     * - some patterns with escaped backslash(\\) followed by wildcards, e.g. R"(\\_a)", do not work correctly due to a bug in LikePredicate.
     */
    DEFINE_VECTORIZED_FN(like);

    static Status like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

} // namespace starrocks
