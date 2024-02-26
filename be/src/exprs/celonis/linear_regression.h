#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisLinearRegression {
public:
    /**
     * @param: [input_x, model]
     * @paramType columns: [DOUBLE, VARCHAR]
     * @return: DOUBLE
     * A valid model should be in format: "intercept:slope".
     */
    DEFINE_VECTORIZED_FN(predict_linear_regression);

    static Status predict_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status predict_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(predict_linear_regression_constant_model);

    DEFINE_VECTORIZED_FN(predict_linear_regression_non_constant_model);
};

} // namespace starrocks