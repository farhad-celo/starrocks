#include "exprs/celonis/kmeans.h"

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

class KmeansModel {

public:
    KmeansModel(const std::vector<std::pair<double, double>>& limits, const std::vector<std::vector<double>>& centroids)
            : limits_(limits), centroids_(centroids) {}

    std::vector<double> normalize_point(const std::vector<double>& point) const {
        std::vector<double> normalized_point = point;
        const double epsilon = 1e-9;
        for (auto i = 0; i < normalized_point.size(); ++i) {
            const auto [min_value, max_value] = limits_[i];
            if (max_value - min_value < epsilon) {
                normalized_point[i] = 0.0;
            } else {
                double value_range = max_value - min_value;
                normalized_point[i] = (normalized_point[i] - min_value) / value_range;
            }
        }
        return normalized_point;
    }

    StatusOr<int64_t> compute_label(const std::vector<double>& point) const {
        if (point.size() != centroids_[0].size() || point.size() != limits_.size()) {
            return Status::InvalidArgument(
                    "The dimension of the point does not match the dimension of centroids of the model.");
        }
        if (centroids_.size() == 1) {
            return 0;
        }
        const auto normalized_point = normalize_point(point);
        int64_t label = -1;
        double min_distance = std::numeric_limits<double>::max();

        for (auto i = 0; i < centroids_.size(); ++i) {
            double distance = 0.0;
            for (int j = 0; j < normalized_point.size(); ++j) {
                double diff = normalized_point[j] - centroids_[i][j];
                distance += diff * diff;
            }
            if (distance < min_distance) {
                min_distance = distance;
                label = i;
            }
        }
        return label;
    }

private:
    std::vector<std::pair<double, double>> limits_;
    std::vector<std::vector<double>> centroids_;
};

// A valid model should be in format:
// "min_1,max_1;...;min_m,max_m:x_11,x_12,...,x_1m;x_21,x_22,...,x_2m;...;x_k1,x_k2,...,x_km".
bool parse_model(const std::string& model, std::vector<std::pair<double, double>>& limits,
                 std::vector<std::vector<double>>& centroids) {
    std::vector<std::string> parts;
    boost::split(parts, model, boost::is_any_of(":"));
    if (parts.size() != 2) {
        return false;
    }
    const std::string& limits_str = parts[0];
    const std::string& centroids_str = parts[1];
    std::vector<std::string> limit_rows;
    boost::split(limit_rows, limits_str, boost::is_any_of(";"));
    limits.clear();
    for (size_t row = 0; row < limit_rows.size(); ++row) {
        std::vector<std::string> values;
        boost::split(values, limit_rows[row], boost::is_any_of(","));
        if (values.size() != 2) {
            return false;
        }
        double min_value, max_value;
        try {
            min_value = boost::lexical_cast<double>(values[0]);
            max_value = boost::lexical_cast<double>(values[1]);
        } catch (const boost::bad_lexical_cast& e) {
            return false;
        }
        limits.emplace_back(min_value, max_value);
    }
    const auto nfeatures = limits.size();
    std::vector<std::string> rows;
    boost::split(rows, centroids_str, boost::is_any_of(";"));
    centroids.clear();
    for (size_t row = 0; row < rows.size(); ++row) {
        std::vector<std::string> values;
        boost::split(values, rows[row], boost::is_any_of(","));
        if (!centroids.empty() && values.size() != centroids.back().size()) {
            return false;
        }
        if (values.size() != nfeatures) {
            return false;
        }
        std::vector<double> centroid;
        centroid.reserve(values.size());
        for (size_t col = 0; col < values.size(); ++col) {
            try {
                auto value = boost::lexical_cast<double>(values[col]);
                centroid.push_back(value);
            } catch (const boost::bad_lexical_cast& e) {
                return false;
            }
        }
        centroids.emplace_back(centroid);
    }
    return true;
}

struct KmeansStateFragmentLocal {
    bool is_valid = false;
    std::optional<KmeansModel> model = std::nullopt;
    ScalarFunction function;
};

} // namespace

Status CelonisKmeans::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL || context->get_num_args() != 2) {
        return Status::OK();
    }

    auto state = new KmeansStateFragmentLocal();
    context->set_function_state(scope, state);

    auto model_column = context->get_constant_column(1);
    if (model_column == nullptr) {
        state->function = apply_kmeans_non_constant_model;
        return Status::OK();
    }
    state->function = apply_kmeans_constant_model;

    if (model_column->size() == 0) {
        return Status::OK();
    }
    if (model_column->is_null(0)) {
        return Status::OK();
    }
    const std::string model = model_column->get(0).get_slice().to_string();
    std::vector<std::vector<double>> centroids;
    std::vector<std::pair<double, double>> limits;
    const bool is_valid = parse_model(model, limits, centroids);
    if (is_valid) {
        state->is_valid = true;
        state->model = KmeansModel(limits, centroids);
    }
    return Status::OK();
}

Status CelonisKmeans::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const KmeansStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr>
CelonisKmeans::apply_kmeans_non_constant_model([[maybe_unused]]FunctionContext* context,
                                               const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto& model_column = columns[1];
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer<TYPE_VARCHAR> model_viewer(model_column);

    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<TYPE_DOUBLE>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        std::vector<double> point;
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        bool any_null = false;
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                any_null = true;
                break;
            }
            point.push_back(elements[i]);
        }
        if (any_null) {
            result.append_null();
            continue;
        }
        const std::string model_str = model_viewer.value(row).to_string();
        std::vector<std::vector<double>> centroids;
        std::vector<std::pair<double, double>> limits;
        const bool is_valid = parse_model(model_str, limits, centroids);
        if (!is_valid) {
            result.append_null();
            continue;
        }
        const auto model = KmeansModel(limits, centroids);
        const auto label = model.compute_label(point);
        if (label.ok()) {
            result.append(label.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr>
CelonisKmeans::apply_kmeans_constant_model([[maybe_unused]]FunctionContext* context,
                                           const Columns& columns) {

    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    const auto* state = reinterpret_cast<const KmeansStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<TYPE_DOUBLE>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (!state->is_valid) {
            result.append_null();
            continue;
        }
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        std::vector<double> point;
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        bool any_null = false;
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                any_null = true;
                break;
            }
            point.push_back(elements[i]);
        }
        if (any_null) {
            result.append_null();
            continue;
        }
        const auto label = state->model->compute_label(point);
        if (label.ok()) {
            result.append(label.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr>
CelonisKmeans::apply_kmeans_model(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    const auto* state = reinterpret_cast<const KmeansStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
