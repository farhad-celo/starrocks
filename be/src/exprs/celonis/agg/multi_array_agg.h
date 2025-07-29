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

#pragma once

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/hash_set.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exec/sorting/sorting.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"
#include "types/logical_type.h"
#include "util/defer_op.h"

namespace starrocks {

namespace {
template<typename T>
bool is_capacity_limit_reached(const std::shared_ptr<T>& column) {
    auto result = column->capacity_limit_reached();
    if constexpr (std::is_same_v<decltype(result), bool>) {
        return result;
    } else {
        return !result.ok();
    }
}
template<typename T>
bool is_capacity_limit_reached(T&& column) {
    if constexpr (requires { column->capacity_limit_reached(); }) {
        // Has -> operator (pointers, smart pointers)
        auto result = column->capacity_limit_reached();
        if constexpr (std::is_same_v<decltype(result), bool>) {
            return result;
        } else {
            return !result.ok();
        }
    } else {
        // Direct object access
        auto result = column.capacity_limit_reached();
        if constexpr (std::is_same_v<decltype(result), bool>) {
            return result;
        } else {
            return !result.ok();
        }
    }
}
}

// input columns result in intermediate result: struct{array[col0], array[col1], array[col2]... array[coln]}
struct MultiArrayAggAggregateState {

    MultiArrayAggAggregateState() : size_limit(config::array_agg_size_limit),
                                    serialization_threshold(config::multi_array_agg_serialization_threshold) {}

    void create_columns(FunctionContext* ctx) {
        auto num = ctx->get_num_args();
        data_columns.reserve(num);
        for (auto i = 0; i < num; ++i) {
            data_columns.emplace_back(ctx->create_column(*ctx->get_arg_type(i), true));
        }
    };

    void clear_serialized_data() {
        serialized_data.clear();
        num_serialized_rows = 0;
    }

    bool size_limit_reached() const {
        return (!data_columns.empty() && data_columns[0]->size() > size_limit) || num_serialized_rows > size_limit;
    }

    void deserialize_data(Columns& columns) {
        const uint8_t* pos = serialized_data.data();
        auto end = pos + serialized_data.size();
        while (pos < end) {
            size_t index;
            memcpy(&index, pos, sizeof(size_t));
            pos += sizeof(size_t);
            size_t count;
            memcpy(&count, pos, sizeof(size_t));
            pos += sizeof(size_t);
            for (auto i = 0; i < count; ++i) {
                pos = columns[index]->deserialize_and_append(pos);
            }
        }
        DCHECK_EQ(pos, end);
    }

    void init_columns(FunctionContext* ctx) {
        create_columns(ctx);
        deserialize_data(data_columns);
        clear_serialized_data();
    }

    void update(FunctionContext* ctx, const Column& column, size_t index, size_t offset, size_t count) {
        // switch to use data_columns
        if (data_columns.empty() && num_serialized_rows + count > serialization_threshold) {
            init_columns(ctx);
        }
        if (data_columns.empty()) {
            // TODO(y.zhang): Get rid of the overhead.
            // We can not use column->serialize(...) to serialize the data directly. serialize() is not a const method.
            auto temp_column = ctx->create_column(*ctx->get_arg_type(index), true);
            temp_column->append(column, offset, count);
            serialize_data(temp_column, index, 0, count);
        } else {
            data_columns[index]->append(column, offset, count);
        }
    }

    void update_nulls(FunctionContext* ctx, size_t index, size_t count) {
        // switch to use data_columns
        if (data_columns.empty() && num_serialized_rows + count > serialization_threshold) {
            init_columns(ctx);
        }
        if (data_columns.empty()) {
            auto temp_column = ctx->create_column(*ctx->get_arg_type(index), true);
            temp_column->append_nulls(count);
            serialize_data(temp_column, index, 0, count);
        } else {
            data_columns[index]->append_nulls(count);
        }
    }

    bool check_overflow(FunctionContext* ctx) const {
        for (size_t i = 0; i < data_columns.size(); i++) {
            if (UNLIKELY(is_capacity_limit_reached(data_columns[i]))) {
                ctx->set_error("The column generated by multi_array_agg is overflow");
                return true;
            }
        }
        return false;
    }

    static bool check_overflow(const Column& col, FunctionContext* ctx) {
        if (UNLIKELY(is_capacity_limit_reached(col))) {
            ctx->set_error("The column generated by multi_array_agg is overflow");
            return true;
        }
        return false;
    }

    // release the trailing N-num_cols order-by columns
    void release_order_by_columns(size_t num_cols) {
        if (data_columns.empty()) {
            return;
        }
        for (auto i = num_cols; i < data_columns.size(); ++i) {
            data_columns[i].reset();
        }
        data_columns.resize(num_cols);
    }

    void release_data_column(size_t index) {
        if (data_columns.empty() || index >= data_columns.size()) {
            return;
        }
        data_columns[index].reset();
    }

    // using pointer rather than vector to avoid variadic size
    // multi_array_agg(a, b order by c, d), the a,b,c,d are put into data_columns in order.
    // When the number of rows is small, it is not memory efficient to use data_columns.
    // If number of rows <= serialization_threshold, serialized_data is used to manage the data.
    Columns data_columns;
    int64_t size_limit;
    int32_t serialization_threshold;
    std::vector<uint8> serialized_data;
    int32_t num_serialized_rows = 0;

private:

    void serialize_data(const ColumnPtr& column, size_t index, size_t offset, size_t count) {
        // size_t index,
        // size_t count,
        // serialize elements in [offset, offset + count)
        if (index == 0) {
            num_serialized_rows += count;
        }
        size_t req_size = sizeof(size_t) * 2;
        for (auto i = offset; i < offset + count; ++i) {
            req_size += column->serialize_size(i);
        }
        auto old_size = serialized_data.size();
        serialized_data.resize(old_size + req_size);
        auto pos = serialized_data.data() + old_size;
        memcpy(pos, &index, sizeof(size_t));
        pos += sizeof(index);
        memcpy(pos, &count, sizeof(size_t));
        pos += sizeof(count);
        for (auto i = offset; i < offset + count; ++i) {
            auto size_written = column->serialize(i, pos);
            pos += size_written;
        }
        DCHECK_EQ(pos, serialized_data.data() + serialized_data.size());
    }

};

class MultiArrayAggAggregateFunction
        : public AggregateFunctionBatchHelper<MultiArrayAggAggregateState, MultiArrayAggAggregateFunction> {
private:
    size_t get_num_agg_columns(FunctionContext* ctx) const {
        return ctx->get_num_args() - ctx->get_is_asc_order().size();
    }

public:
    void create(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        auto* state = new(ptr) MultiArrayAggAggregateState;
        DCHECK(state->data_columns.empty());
    }

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& state_impl = this->data(state);
        if (!state_impl.data_columns.empty()) {
            for (auto& col: state_impl.data_columns) {
                col->resize(0);
            }
        }
        state_impl.clear_serialized_data();
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        if (UNLIKELY(this->data(state).size_limit_reached())) {
            ctx->set_error(("size limit (" + std::to_string(this->data(state).size_limit) +
                            ") of multi_array_agg is reached").c_str());
            return;
        }
        for (auto i = 0; i < ctx->get_num_args(); ++i) {
            if (UNLIKELY(columns[i]->size() <= row_num)) {
                ctx->set_error(std::string(get_name() + "'s update row number overflow").c_str(), false);
                return;
            }
            // TODO: update is random access, so we could not pre-reserve memory for State, which is the bottleneck
            if ((columns[i]->is_nullable() && columns[i]->is_null(row_num)) || columns[i]->only_null()) {
                this->data(state).update_nulls(ctx, i, 1);
                continue;
            }
            auto* data_col = columns[i];
            auto tmp_row_num = row_num;
            if (columns[i]->is_constant()) {
                // just copy the first const value.
                data_col = down_cast<const ConstColumn*>(columns[i])->data_column().get();
                tmp_row_num = 0;
            }
            this->data(state).update(ctx, *data_col, i, tmp_row_num, 1);
        }
    }

    // struct and array elements aren't be null, as they consist from several columns
    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
        for (auto i = 0; i < input_columns.size(); ++i) {
            auto array_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns[i].get()));
            auto& offsets = array_column->offsets().get_data();
            this->data(state).update(ctx, array_column->elements(), i, offsets[row_num],
                                     offsets[row_num + 1] - offsets[row_num]);
        }
    }

    // serialize each state->column to a [nullable] array in a [nullable] struct
    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& state_impl = this->data(const_cast<AggDataPtr>(state));
        // should check overflow before append, otherwise will generate invalid result.
        if (UNLIKELY(state_impl.check_overflow(ctx))) {
            return;
        }
        if (state_impl.data_columns.empty()) {
            state_impl.init_columns(ctx);
        }
        auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        for (auto i = 0; i < columns.size(); ++i) {
            auto elem_size = state_impl.data_columns.at(i)->size();
            auto array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(columns[i].get()));
            if (columns[i]->is_nullable()) {
                down_cast<NullableColumn*>(columns[i].get())->null_column_data().emplace_back(0);
            }
            if (state_impl.data_columns.at(i)->only_null()) {
                array_col->elements_column()->append_nulls(elem_size);
            } else {
                array_col->elements_column()->append(
                        *ColumnHelper::unpack_and_duplicate_const_column(elem_size, state_impl.data_columns.at(i)), 0,
                        elem_size);
            }
            auto& offsets = array_col->offsets_column()->get_data();
            offsets.push_back(offsets.back() + elem_size);
            state_impl.data_columns[i].reset();
        }
        state_impl.data_columns.clear();

        // should check overflow after append, otherwise the result column with multi row will be overflow.
        if (UNLIKELY(state_impl.check_overflow(*to, ctx))) {
            return;
        }
    }

    // finalize each state->column to a [nullable] array
    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto defer = DeferOp([&]() {
            if (ctx->has_error() && to != nullptr) {
                to->append_default();
            }
        });
        if (UNLIKELY(!ColumnHelper::get_data_column(to)->is_struct())) {
            ctx->set_error(std::string("The output column of " + get_name() +
                                       " finalize_to_column() is not struct, but is " + to->get_name())
                                   .c_str(),
                           false);
            return;
        }
        auto& state_impl = this->data(const_cast<AggDataPtr>(state));
        if (state_impl.data_columns.empty()) {
            state_impl.init_columns(ctx);
        }
        if (UNLIKELY(state_impl.size_limit_reached())) {
            ctx->set_error(("size limit (" + std::to_string(state_impl.size_limit) +
                            ") of multi_array_agg is reached").c_str());
            return;
        }
        // should check overflow before append, otherwise will generate invalid result.
        if (UNLIKELY(state_impl.check_overflow(ctx))) {
            return;
        }
        const auto num_agg_columns = get_num_agg_columns(ctx);
        auto sort_start_time = std::chrono::high_resolution_clock::now();
        Permutation perm;
        if (!ctx->get_is_asc_order().empty()) {
            Columns order_by_columns;
            SortDescs sort_desc(ctx->get_is_asc_order(), ctx->get_nulls_first());
            order_by_columns.assign(state_impl.data_columns.begin() + num_agg_columns, state_impl.data_columns.end());
            Status st = sort_and_tie_columns(ctx->state()->cancelled_ref(), order_by_columns, sort_desc, &perm);
            // release order-by columns early
            order_by_columns.clear();
            state_impl.release_order_by_columns(num_agg_columns);
            if (UNLIKELY(ctx->state()->cancelled_ref())) {
                ctx->set_error("multi_array_agg detects cancelled.", false);
                return;
            }
            if (UNLIKELY(!st.ok())) {
                ctx->set_error(st.to_string().c_str(), false);
                return;
            }
        }
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
        Buffer<uint32_t> index;
        auto elem_size = state_impl.data_columns[0]->size();
        if (!perm.empty()) {
            auto res_num = 0;
            index.resize(elem_size);
            for (auto row_id = 0; row_id < elem_size; row_id++) {
                index[res_num++] = perm[row_id].index_in_chunk;
            }
            index.resize(res_num);
            elem_size = res_num;
        }
        auto sort_end_time = std::chrono::high_resolution_clock::now();
        auto sort_duration = std::chrono::duration_cast<std::chrono::microseconds>(sort_end_time - sort_start_time);
        if (elem_size > 1000000) {
            LOG(INFO) << "MULTI_ARRAY_AGG (finalize_to_column) num of rows = " << elem_size << std::endl;
            LOG(INFO) << "MULTI_ARRAY_AGG (finalize_to_column) sorting time = " << sort_duration.count() << " us\n";
        }

        auto output_start_time = std::chrono::high_resolution_clock::now();
        for (auto i = 0; i < num_agg_columns; ++i) {
            DCHECK_EQ(state_impl.data_columns[i]->size(), elem_size);
            auto& to_column = columns[i];
            if (to_column->is_nullable()) {
                down_cast<NullableColumn*>(to_column.get())->null_column_data().emplace_back(0);
            }
            auto& res = state_impl.data_columns[i];
            auto array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(to_column.get()));
            DCHECK(!res->is_constant());
            if (index.empty()) {
                array_col->elements_column()->append(*res, 0, elem_size);
            } else {
                array_col->elements_column()->append_selective(*res, index);
            }
            auto& offsets = array_col->offsets_column()->get_data();
            offsets.push_back(offsets.back() + elem_size);
            state_impl.release_data_column(i);
        }
        auto output_end_time = std::chrono::high_resolution_clock::now();
        auto output_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                output_end_time - output_start_time);
        if (elem_size > 1000000) {
            LOG(INFO) << "MULTI_ARRAY_AGG (finalize_to_column) output time = " << output_duration.count() << " us\n";
        }
        state_impl.data_columns.clear(); // early release memory
        // should check overflow after append, otherwise the result column with multi row will be overflow.
        if (UNLIKELY(state_impl.check_overflow(*to, ctx))) {
            return;
        }
    }

    // convert each cell of a row to a [nullable] array in a struct
    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        auto columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(dst->get()))->fields_column();
        if (dst->get()->is_nullable()) {
            for (size_t i = 0; i < chunk_size; i++) {
                down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
            }
        }
        for (auto j = 0; j < columns.size(); ++j) {
            auto array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(columns[j].get()));
            if (columns[j].get()->is_nullable()) {
                for (size_t i = 0; i < chunk_size; i++) {
                    down_cast<NullableColumn*>(columns[j].get())->null_column_data().emplace_back(0);
                }
            }
            auto& element_column = array_col->elements_column();
            auto& offsets = array_col->offsets_column()->get_data();
            for (size_t i = 0; i < chunk_size; i++) {
                element_column->append_datum(src[j]->get(i));
                offsets.emplace_back(offsets.back() + 1);
            }
        }
    }

    std::string get_name() const override { return "multi_array_agg"; }
};

} // namespace starrocks