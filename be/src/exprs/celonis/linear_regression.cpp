#include "exprs/celonis/linear_regression.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

namespace starrocks {

namespace {

class LinearRegressionModel {

public:
    LinearRegressionModel(double intercept, const std::vector<double>& coefficients) : intercept_(intercept),
                                                                                       coefficients_(coefficients) {}

    StatusOr<double> predict(const std::vector<double>& x) const {
        if (x.size() != coefficients_.size()) {
            return Status::InvalidArgument("The size of x does not match the number of features in the model.");
        }
        double y = intercept_;
        for (size_t i = 0; i < x.size(); ++i) {
            y += x[i] * coefficients_[i];
        }
        return y;
    }

private:
    double intercept_;
    std::vector<double> coefficients_;
};

// A valid model should be in format: "intercept:coefficient_1:coefficient_2,...,coefficient_n".
bool parse_model(const std::string& model, double& intercept, std::vector<double>& coefficients) {
    std::vector<std::string> parts;
    boost::split(parts, model, boost::is_any_of(":"));
    if (parts.size() < 2) {
        return false;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        try {
            auto value = boost::lexical_cast<double>(parts[i]);
            if (i == 0) {
                intercept = value;
            } else {
                coefficients.push_back(value);
            }
        } catch (const boost::bad_lexical_cast& e) {
            return false;
        }
    }
    return true;
}

struct LinearRegressionStateFragmentLocal {
    bool is_valid = false;
    std::optional<LinearRegressionModel> model = std::nullopt;
    ScalarFunction function;
};

} // namespace

Status CelonisLinearRegression::predict_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL || context->get_num_args() != 2) {
        return Status::OK();
    }

    auto state = new LinearRegressionStateFragmentLocal();
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
    double intercept = 0.0;
    std::vector<double> coefficients;
    const bool is_valid = parse_model(model, intercept, coefficients);
    if (is_valid) {
        state->is_valid = true;
        state->model = LinearRegressionModel(intercept, coefficients);
    }
    return Status::OK();
}

Status CelonisLinearRegression::predict_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const LinearRegressionStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr>
CelonisLinearRegression::predict_linear_regression_non_constant_model([[maybe_unused]]FunctionContext* context,
                                                                      const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto& model_column = columns[1];
    const auto num_rows = model_column->size();
    ColumnViewer<TYPE_VARCHAR> model_viewer(model_column);

    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<TYPE_DOUBLE>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_DOUBLE> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        std::vector<double> xs;
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        bool any_null = false;
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                any_null = true;
                break;
            }
            xs.push_back(elements[i]);
        }
        if (any_null) {
            result.append_null();
            continue;
        }
        const std::string model_str = model_viewer.value(row).to_string();
        double intercept = 0.0;
        std::vector<double> coefficients;
        const bool is_valid = parse_model(model_str, intercept, coefficients);
        if (!is_valid) {
            result.append_null();
            continue;
        }
        const auto model = LinearRegressionModel(intercept, coefficients);
        const auto y = model.predict(xs);
        if (y.ok()) {
            result.append(y.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisLinearRegression::predict_linear_regression_constant_model([[maybe_unused]]FunctionContext* context,
                                                                  const Columns& columns) {

    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto num_rows = columns[0]->size();
    const auto* state = reinterpret_cast<const LinearRegressionStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<TYPE_DOUBLE>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_DOUBLE> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (!state->is_valid) {
            result.append_null();
            continue;
        }
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        std::vector<double> xs;
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        bool any_null = false;
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                any_null = true;
                break;
            }
            xs.push_back(elements[i]);
        }
        if (any_null) {
            result.append_null();
            continue;
        }
        const auto y = state->model->predict(xs);
        if (y.ok()) {
            result.append(y.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisLinearRegression::predict_linear_regression(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    const auto* state = reinterpret_cast<const LinearRegressionStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
