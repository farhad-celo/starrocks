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

#include <google/protobuf/util/json_util.h>

#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/ublas/lu.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/vector.hpp>

#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/util.h"
#include "modules/query/calendars.pb.h"

namespace starrocks {

using namespace boost::numeric::ublas;

namespace {

// precision used when converting doubles to string during generating model string.
static const int PRECISION = 17;

// Performs in-place LU factorization and then solve for X in AX=B
bool lu_solve(const matrix<double>& A, const boost::numeric::ublas::vector<double>& B,
              boost::numeric::ublas::vector<double>& X) {
    matrix<double> A_lu(A);
    permutation_matrix<std::size_t> perm(A.size1());
    int res = lu_factorize(A_lu, perm);
    if (res != 0) return false;
    X.assign(B);
    lu_substitute(A_lu, perm, X);
    return true;
}

std::string to_model_str(const boost::numeric::ublas::vector<double>& beta) {
    std::string sep = "";
    std::string rv = "";
    for (double v : beta) {
        rv += sep;
        rv += double_to_string(v, PRECISION);
        sep = ":";
    }
    return rv;
}

} // namespace

LinearRegressionAggregateState::~LinearRegressionAggregateState() {
    if (x != nullptr) {
        x.reset(nullptr);
    }
    if (y != nullptr) {
        y.reset(nullptr);
    }
}

void LinearRegressionAggregateFunction::create(FunctionContext* ctx, AggDataPtr __restrict ptr) const {
    DCHECK(ctx->get_num_args() == 2);
    auto* state = new (ptr) LinearRegressionAggregateState;
    state->x = std::make_unique<ArrayColumn>(NullableColumn::create(DoubleColumn::create(), NullColumn::create()),
                                             UInt32Column::create());
    state->y = std::make_unique<DoubleColumn>();
}

void LinearRegressionAggregateFunction::reset(FunctionContext* ctx, const Columns& args,
                                              AggDataPtr __restrict state) const {
    auto& state_impl = this->data(state);
    if (state_impl.x != nullptr) {
        state_impl.x.reset(nullptr);
    }
    if (state_impl.y != nullptr) {
        state_impl.y.reset(nullptr);
    }
}

void LinearRegressionAggregateFunction::update(FunctionContext* ctx, const Column** columns,
                                               AggDataPtr __restrict state, size_t row_num) const {
    DCHECK(ctx->get_num_args() == 2);
    for (auto i = 0; i < 2; ++i) {
        if (UNLIKELY(columns[i]->size() <= row_num)) {
            ctx->set_error(std::string(get_name() + "'s update row number overflow").c_str(), false);
            return;
        }
    }
    // x_array or y is NULL, ignore.
    for (auto i = 0; i < 2; ++i) {
        if ((columns[i]->is_nullable() && columns[i]->is_null(row_num)) || columns[i]->only_null()) {
            return;
        }
    }
    // if x_array contains NULL, ignore
    bool has_null = false;
    auto array = columns[0]->get(row_num).get_array();
    for (const auto& v : array) {
        if (v.is_null()) {
            has_null = true;
            break;
        }
    }
    if (has_null) {
        return;
    }
    auto& state_impl = this->data(state);
    state_impl.x->append_datum(columns[0]->get(row_num));
    state_impl.y->append_datum(columns[1]->get(row_num));
}

void LinearRegressionAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                              size_t row_num) const {
    if (column->is_nullable() && column->is_null(row_num)) {
        return;
    }
    auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
    auto& state_impl = this->data(state);
    auto x_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(0).get()));
    auto y_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(1).get()));
    auto& x_offsets = x_column->offsets().get_data();
    const auto x_start = x_offsets[row_num];
    const auto x_end = x_offsets[row_num + 1];
    auto& y_offsets = y_column->offsets().get_data();
    const auto y_start = y_offsets[row_num];
    const auto y_end = y_offsets[row_num + 1];
    DCHECK((x_end - x_start) % (y_end - y_start) == 0);
    const auto n_features = (x_end - x_start) / (y_end - y_start);
    for (auto i = y_start; i < y_end; ++i) {
        state_impl.y->append_datum(y_column->elements().get(i));
        DatumArray x_array;
        const auto start = x_start + n_features * (i - y_start);
        const auto end = start + n_features;
        for (auto j = start; j < end; ++j) {
            x_array.push_back(x_column->elements().get(j));
        }
        state_impl.x->append_datum(x_array);
    }
}

void LinearRegressionAggregateFunction::serialize_to_column(starrocks::FunctionContext* ctx,
                                                            __restrict starrocks::ConstAggDataPtr state,
                                                            starrocks::Column* to) const {
    auto& state_impl = this->data(state);
    auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    if (!state_impl.x->empty()) {
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        // y
        ::starrocks::serialize_to_column(state_impl.y, columns.at(1));
        // x
        if (columns.at(0)->is_nullable()) {
            down_cast<NullableColumn*>(columns.at(0).get())->null_column_data().emplace_back(0);
        }
        auto x_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(columns.at(0).get()));
        const auto x_size = state_impl.x->size();
        size_t size = 0;
        auto& elements = x_col->elements_column();
        for (auto i = 0; i < x_size; ++i) {
            auto array = state_impl.x->get(i).get_array();
            for (auto j = 0; j < array.size(); ++j) {
                elements->append_datum(array[j]);
                ++size;
            }
        }
        auto& offsets = x_col->offsets_column()->get_data();
        offsets.push_back(offsets.back() + size);
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
    std::optional<size_t> num_features = std::nullopt;
    bool len_inconsistent = false;
    for (auto i = 0; i < size; ++i) {
        if (num_features.has_value()) {
            if (num_features != state_impl.x->get(i).get_array().size()) {
                len_inconsistent = true;
                break;
            }
        } else {
            num_features = state_impl.x->get(i).get_array().size();
        }
    }
    if (!num_features.has_value() || len_inconsistent || num_features < 1) {
        to->append_datum(kNullDatum);
        return;
    }
    matrix<double> X(size, num_features.value() + 1);
    boost::numeric::ublas::vector<double> y(size);
    boost::numeric::ublas::vector<double> beta(num_features.value() + 1);
    for (auto i = 0; i < size; ++i) {
        X(i, 0) = 1.0;
        for (auto j = 0; j < num_features.value(); ++j) {
            X(i, j + 1) = state_impl.x->get(i).get_array()[j].get_double();
        }
        y(i) = state_impl.y->get(i).get_double();
    }
    // compute (X^T * X)
    matrix<double> XtX = prod(trans(X), X);

    // compute (X^T * y)
    boost::numeric::ublas::vector<double> Xty = prod(trans(X), y);
    // solve for beta using LU decomposition
    if (lu_solve(XtX, Xty, beta)) {
        const std::string model = to_model_str(beta);
        to->append_datum(model.c_str());
    } else {
        to->append_datum(kNullDatum);
    }
}

// convert each cell of a row to a [nullable] array in a struct
void LinearRegressionAggregateFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                    size_t chunk_size, ColumnPtr* dst) const {
    DCHECK(src.size() == 2);
    std::vector<size_t> valid_indexes;
    for (size_t row = 0; row < chunk_size; ++row) {
        if ((src[0]->is_nullable() && src[0]->is_null(row)) || src[0]->only_null()) {
            continue;
        }
        if ((src[1]->is_nullable() && src[1]->is_null(row)) || src[1]->only_null()) {
            continue;
        }
        bool has_null = false;
        auto array = src[0]->get(row).get_array();
        for (const auto& v : array) {
            if (v.is_null()) {
                has_null = true;
                break;
            }
        }
        if (has_null) {
            continue;
        }
        valid_indexes.push_back(row);
    }
    auto columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(dst->get()))->fields_column();
    if (!valid_indexes.empty()) {
        if (dst->get()->is_nullable()) {
            down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
        }
        DatumArray x_array;
        DatumArray y_array;
        for (auto i : valid_indexes) {
            auto array = src[0]->get(i).get_array();
            const auto length = array.size();
            for (auto j = 0; j < length; ++j) {
                x_array.push_back(array[j]);
            }
            y_array.push_back(src[1]->get(i));
        }
        columns[0]->append_datum(x_array);
        columns[1]->append_datum(y_array);
    }
}

std::string LinearRegressionAggregateFunction::get_name() const {
    return "celonis_build_linear_regression_model";
}

} // namespace starrocks
