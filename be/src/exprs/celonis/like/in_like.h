#include "common/status.h"
#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks::celonis::like {

namespace v2 {
class CelonisInLike {
public:
    static Status in_like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    /**
     * @param: [input_string, patterns]
     * @paramType: [VARCHAR, ARRAY_VARCHAR]
     * @return: BIGINT
     * Implements PQL IN_LIKE https://docs.celonis.com/en/in_like.html
     */
    DEFINE_VECTORIZED_FN(in_like);

    static Status in_like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};
} // namespace v2

namespace v1 {
class CelonisInLike {
public:
    static Status in_like_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    /**
     * @param: [input_string, patterns]
     * @paramType: [VARCHAR, ARRAY_VARCHAR]
     * @return: BIGINT
     * Implements PQL IN_LIKE https://docs.celonis.com/en/in_like.html
     */
    DEFINE_VECTORIZED_FN(in_like);

    static Status in_like_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

StatusOr<ColumnPtr> in_like_non_constant_patterns(FunctionContext* context, const Columns& columns);

} // namespace v1

} // namespace starrocks::celonis::like
