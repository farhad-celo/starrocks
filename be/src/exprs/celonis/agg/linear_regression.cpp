// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "linear_regression.h"
#include "modules/query/calendars.pb.h"
#include <google/protobuf/util/json_util.h>

namespace starrocks {

namespace {

double mean(const std::vector<double>& v) {
    double sum = 0.0;
    for (auto& i: v) sum += i;
    return sum / v.size();
}

double covariance(const std::vector<double>& x, const std::vector<double>& y) {
    double sum = 0.0;
    double x_mean = mean(x);
    double y_mean = mean(y);
    for (size_t i = 0; i < x.size(); i++) {
        sum += (x[i] - x_mean) * (y[i] - y_mean);
    }
    return sum;
}

double variance(const std::vector<double>& v) {
    double x_mean = mean(v);
    double sum = 0.0;
    for (auto& i: v) {
        sum += (i - x_mean) * (i - x_mean);
    }
    return sum;
}

std::pair<double, double> linear_regression(const std::vector<double>& x, const std::vector<double>& y) {
    double b1 = covariance(x, y) / variance(x);
    double b0 = mean(y) - b1 * mean(x);
    return std::make_pair(b0, b1); // Returns a pair of coefficients (intercept, slope)
}

}

LinearRegressionAggregateState::~LinearRegressionAggregateState() {
    if (x != nullptr) {
        x.reset(nullptr);
    }
    if (y != nullptr) {
        y.reset(nullptr);
    }
}

void LinearRegressionAggregateFunction::create(FunctionContext* ctx, AggDataPtr __restrict ptr) const {
    auto num = ctx->get_num_args();
    DCHECK(num == 2);
    auto* state = new(ptr) LinearRegressionAggregateState;
    state->x = std::make_unique<DoubleColumn>();
    state->y = std::make_unique<DoubleColumn>();
}

void
LinearRegressionAggregateFunction::reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const {
    auto& state_impl = this->data(state);
    if (state_impl.x != nullptr) {
        state_impl.x.reset(nullptr);
    }
    if (state_impl.y != nullptr) {
        state_impl.y.reset(nullptr);
    }
}

void
LinearRegressionAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                                          size_t row_num) const {
    DCHECK(ctx->get_num_args() == 2);
    for (auto i = 0; i < ctx->get_num_args(); ++i) {
        if (UNLIKELY(columns[i]->size() <= row_num)) {
            ctx->set_error(std::string(get_name() + "'s update row number overflow").c_str(), false);
            return;
        }
    }
    // x or y is NULL, ignore.
    for (auto i = 0; i < 2; ++i) {
        if ((columns[i]->is_nullable() && columns[i]->is_null(row_num)) || columns[i]->only_null()) {
            return;
        }
    }

    auto& state_impl = this->data(state);
    state_impl.x->append_datum(columns[0]->get(row_num));
    state_impl.y->append_datum(columns[1]->get(row_num));
}

void LinearRegressionAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                              size_t row_num) const {
    auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
    auto& state_impl = this->data(state);
    state_impl.x->append_datum(input_columns.at(0)->get(row_num));
    state_impl.y->append_datum(input_columns.at(1)->get(row_num));
}

void LinearRegressionAggregateFunction::serialize_to_column(starrocks::FunctionContext* ctx,
                                                            __restrict starrocks::ConstAggDataPtr state,
                                                            starrocks::Column* to) const {
    auto& state_impl = this->data(state);
    auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    const auto size = state_impl.x->size();
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->mutable_null_column()->get_data().resize(size, 0);
    }
    down_cast<NullableColumn*>(columns[0].get())->mutable_null_column()->get_data().resize(size, 0);
    down_cast<NullableColumn*>(columns[1].get())->mutable_null_column()->get_data().resize(size, 0);
    auto x = down_cast<DoubleColumn*>(ColumnHelper::get_data_column(columns[0].get()));
    for (size_t i = 0; i < size; ++i) {
        x->append(state_impl.x->get(i).get_double());
    }
    auto y = down_cast<DoubleColumn*>(ColumnHelper::get_data_column(columns[1].get()));
    for (size_t i = 0; i < size; ++i) {
        y->append(state_impl.y->get(i).get_double());
    }
}

void LinearRegressionAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                           Column* to) const {
    auto defer = DeferOp([&]() {
        if (ctx->has_error() && to != nullptr) {
            to->append_default();
        }
    });
    if (UNLIKELY(!ColumnHelper::get_data_column(to)->is_binary())) {
        ctx->set_error(std::string("The output column of " + get_name() +
                                   " finalize_to_column() is not varchar, but is " + to->get_name())
                               .c_str(),
                       false);
        return;
    }
    auto& state_impl = this->data(state);
    const auto size = state_impl.x->size();
    DCHECK(state_impl.y->size() == size);
    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(size);
    ys.reserve(size);
    for (int i = 0; i < size; ++i) {
        xs.push_back(state_impl.x->get(i).get_double());
        ys.push_back(state_impl.y->get(i).get_double());
    }
    auto [intercept, slope] = linear_regression(xs, ys);
    if (std::isnan(intercept) || std::isnan(slope)) {
        to->append_datum(kNullDatum);
    } else {
        const std::string model = std::to_string(intercept) + ":" + std::to_string(slope);
        to->append_datum(model.c_str());
    }
}

// convert each cell of a row to a [nullable] array in a struct
void LinearRegressionAggregateFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                    size_t chunk_size,
                                                                    ColumnPtr* dst) const {
    DCHECK(src.size() == 2);
    std::vector<size_t> valid_indexes;
    for (size_t row = 0; row < chunk_size; ++row) {
        if ((src[0]->is_nullable() && src[0]->is_null(row)) || src[0]->only_null()) {
            continue;
        }
        if ((src[1]->is_nullable() && src[1]->is_null(row)) || src[1]->only_null()) {
            continue;
        }
        valid_indexes.push_back(row);
    }
    auto columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(dst->get()))->fields_column();
    if (dst->get()->is_nullable()) {
        for (size_t i = 0; i < valid_indexes.size(); i++) {
            down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
        }
    }
    for (auto& column: columns) {
        if (column.get()->is_nullable()) {
            down_cast<NullableColumn*>(column.get())->mutable_null_column()->get_data().resize(valid_indexes.size(), 0);
        }
    }
    auto x = down_cast<DoubleColumn*>(ColumnHelper::get_data_column(columns[0].get()));
    for (auto i: valid_indexes) {
        x->append_datum(src[0]->get(i));
    }
    auto y = down_cast<DoubleColumn*>(ColumnHelper::get_data_column(columns[1].get()));
    for (auto i: valid_indexes) {
        y->append_datum(src[1]->get(i));
    }
}

std::string LinearRegressionAggregateFunction::get_name() const { return "celonis_build_linear_regression_model"; }

} // namespace starrocks
