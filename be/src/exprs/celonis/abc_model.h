#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

template <LogicalType LT>
class CelonisAbcModel {
public:
    /**
     * @param: [input_value, pk_hash, model]
     * @paramType columns: [ BIGINT | DOUBLE, BIGINT, VARCHAR]
     * @return: BIGINT
     */
    DEFINE_VECTORIZED_FN(apply_abc_model);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(apply_abc_model_constant_model);

    DEFINE_VECTORIZED_FN(apply_abc_model_non_constant_model);
};

} // namespace starrocks