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
     * Some patterns with escaped backslash(\\) followed by wildcards, e.g. R"(\\_a)", do not work correctly due to a bug in LikePredicate.
     */
    DEFINE_VECTORIZED_FN(like);

    static Status like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    static std::pair<std::string, bool> convert_like_pattern(const Slice& pattern);

    static StatusOr<ColumnPtr> like_non_constant(FunctionContext* context, const Columns& columns);
    static StatusOr<ColumnPtr> like_constant_no_wildcard(FunctionContext* context, const Columns& columns);
};

} // namespace starrocks
