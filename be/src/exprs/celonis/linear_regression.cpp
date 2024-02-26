#include "exprs/celonis/linear_regression.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

// A valid model should be in format: "intercept:slope".
bool parse_model(const std::string& model, double& intercept, double& slope) {
    std::istringstream iss(model);
    char delim;
    if (!(iss >> intercept >> delim >> slope) || delim != ':') {
        return false;
    }
    return true;
}

double predict(double x, double intercept, double slope) {
    return intercept + x * slope;
}

}

struct LinearRegressionStateThreadLocal {
    bool is_valid = false;
    double intercept = std::numeric_limits<double>::quiet_NaN();
    double slope = std::numeric_limits<double>::quiet_NaN();
    ScalarFunction function;
};

Status CelonisLinearRegression::predict_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::THREAD_LOCAL) {
        return Status::OK();
    }

    auto state = new LinearRegressionStateThreadLocal();
    context->set_function_state(scope, state);

    auto model_column = context->get_constant_column(1);
    // context->is_constant_column(i) must not be used to determine if the argument is Array Literal because as of
    // 2024-02-26 it returns false for Array Literal while get_constant_column(i) returns non nullptr.
    // For the same reason, columns[i]->is_constant() must not be used in celonis_predict_linear_regression.
    if (model_column == nullptr) {
        state->function = predict_linear_regression_non_constant_model;
        return Status::OK();
    }
    state->function = predict_linear_regression_constant_model;

    if (model_column->size() == 0) {
        return Status::OK();
    }

    if (model_column->is_null(0)) {
        return Status::OK();
    }

    const std::string model = model_column->get(0).get_slice().to_string();
    const bool is_valid = parse_model(model, state->intercept, state->slope);
    state->is_valid = is_valid;
    return Status::OK();
}

Status CelonisLinearRegression::predict_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::THREAD_LOCAL) {
        const auto* state = reinterpret_cast<const LinearRegressionStateThreadLocal*>(
                context->get_function_state(FunctionContext::THREAD_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr>
CelonisLinearRegression::predict_linear_regression_non_constant_model([[maybe_unused]]FunctionContext* context,
                                                                      const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    const auto& x_column = columns[0];
    const auto& model_column = columns[1];
    auto num_rows = x_column->size();
    ColumnViewer<TYPE_DOUBLE> x_viewer(x_column);
    ColumnViewer<TYPE_VARCHAR> model_viewer(model_column);
    ColumnBuilder<TYPE_DOUBLE> result(num_rows);

    for (int row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        const std::string model = model_viewer.value(row).to_string();
        double intercept, slope;
        const bool is_valid = parse_model(model, intercept, slope);
        if (!is_valid) {
            result.append_null();
            continue;
        }
        const double x = x_viewer.value(row);
        const auto y = predict(x, intercept, slope);
        result.append(y);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisLinearRegression::predict_linear_regression_constant_model([[maybe_unused]]FunctionContext* context,
                                                                  const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    const auto& x_column = columns[0];
    auto num_rows = x_column->size();
    ColumnViewer<TYPE_DOUBLE> x_viewer(x_column);
    const auto* state = reinterpret_cast<const LinearRegressionStateThreadLocal*>(
            context->get_function_state(FunctionContext::THREAD_LOCAL));

    ColumnBuilder<TYPE_DOUBLE> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        if (!state->is_valid) {
            result.append_null();
            continue;
        }
        const double x = x_viewer.value(row);
        const auto y = predict(x, state->intercept, state->slope);
        result.append(y);
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisLinearRegression::predict_linear_regression(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    const auto* state = reinterpret_cast<const LinearRegressionStateThreadLocal*>(
            context->get_function_state(FunctionContext::THREAD_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
