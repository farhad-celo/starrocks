#pragma once

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exec/sorting/sort_helper.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "types/logical_type_infra.h"

namespace starrocks {

template <bool is_first>
struct CelonisSortedFirstLastAggregateState {
    void set_new_data_columns(FunctionContext* ctx, const Column** columns, size_t row_num) {
        size_t new_buffer_size = 0;
        for (int i = 0; i < ctx->get_num_args(); ++i) {
            if (!columns[i]->is_null(row_num) && ctx->get_arg_type(i)->type == TYPE_VARCHAR) {
                new_buffer_size += columns[i]->get(row_num).get_slice().size;
            }
        }
        size_t offset = 0;
        buffer.resize(new_buffer_size);
        for (int i = 0; i < ctx->get_num_args(); ++i) {
            if (columns[i]->is_null(row_num)) {
                data[i] = kNullDatum;
            } else if (ctx->get_arg_type(i)->type == TYPE_VARCHAR) {
                auto& slice = columns[i]->get(row_num).get_slice();
                memcpy(buffer.data() + offset, slice.data, slice.size);
                data[i] = Slice(buffer.data() + offset, slice.size);
                offset += slice.size;
            } else {
                data[i] = columns[i]->get(row_num);
            }
        }
    }

    void update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        if (data.empty()) {
            data.resize(ctx->get_num_args());
            set_new_data_columns(ctx, columns, row_num);
            return;
        }
        const auto& is_asc_order = ctx->get_is_asc_order();
        const auto& null_firsts = ctx->get_nulls_first();
        for (int i = 1; i < data.size(); ++i) {
            auto order_index = i - 1;
            if (data[i].is_null()) {
                if (columns[i]->is_null(row_num)) {
                    continue;
                }
                if (null_firsts[order_index]) {
                    if constexpr (!is_first) {
                        set_new_data_columns(ctx, columns, row_num);
                    }
                } else {
                    if constexpr (is_first) {
                        set_new_data_columns(ctx, columns, row_num);
                    }
                }
                return;
            }
            if (columns[i]->is_null(row_num)) {
                if (null_firsts[order_index]) {
                    if constexpr (is_first) {
                        set_new_data_columns(ctx, columns, row_num);
                    }
                } else {
                    if constexpr (!is_first) {
                        set_new_data_columns(ctx, columns, row_num);
                    }
                }
                return;
            }
            int cmp = 0;
            auto logical_type = ctx->get_arg_type(i)->type;
            switch (logical_type) {
#define M(type) \
                case type: \
                        cmp = SorterComparator<RunTimeCppType<type>>::compare( \
                                data[i].get<RunTimeCppType<type>>(), \
                                columns[i]->get(row_num).get<RunTimeCppType<type>>()); \
                        break;

                    APPLY_FOR_ALL_NUMBER_TYPE(M)
                    M(TYPE_DATETIME)
                    M(TYPE_VARCHAR)
#undef M
                default:
                    throw std::runtime_error(fmt::format("Unsupported column type {}", logical_type));
                    break;
            }
            if (cmp == 0) {
                continue;
            } else if ((cmp < 0) == is_asc_order[order_index]) {
                if constexpr (!is_first) {
                    set_new_data_columns(ctx, columns, row_num);
                }
                return;
            } else {
                if constexpr (is_first) {
                    set_new_data_columns(ctx, columns, row_num);
                }
                return;
            }
        }
    }

    ~CelonisSortedFirstLastAggregateState() {}

    DatumStruct data;
    raw::RawVector<uint8_t> buffer;
};

/**
 * @param: col [ORDER BY col0 [DESC | ASC] [NULLS FIRST | NULLS LAST] ...]
 * @paramType: Any type
 * @return: col type
 * col: the column whose values to choose as first or last.
        Rows with NULL values are ignored, so they do not influence the result.
        If all the values are NULL, the result is NULL.
 * col0: the column which decides the order of col. There may be more than one ORDER BY column.
         [DESC | ASC]: specifies whether to sort the elements in ascending order (default) or descending order of col0.
         [NULLS FIRST | NULLS LAST]: specifies whether NULL values are placed at the first or last place.
                                     If not specified, NULL is considered less than everything other.
 */
template <bool is_first>
class CelonisSortedFirstLastAggregateFunction
        : public AggregateFunctionBatchHelper<CelonisSortedFirstLastAggregateState<is_first>,
                                              CelonisSortedFirstLastAggregateFunction<is_first>> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& state_impl = this->data(state);
        state_impl.data.clear();
        state_impl.buffer.clear();
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        if (columns[0]->is_null(row_num)) return;
        this->data(state).update(ctx, columns, row_num);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        if (column->is_null(row_num)) {
            return;
        }
        auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
        std::vector<const Column*> columns;
        for (auto i = 0; i < input_columns.size(); ++i) {
            columns.push_back(input_columns[i].get());
        }
        this->data(state).update(ctx, columns.data(), row_num);
    }

    // serialize each state->column to a field in a [nullable] struct
    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& state_impl = this->data(state);
        auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
        if (state_impl.data.empty()) {
            to->append_default();
            return;
        }
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        for (auto i = 0; i < columns.size(); ++i) {
            columns[i]->append_datum(state_impl.data[i]);
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& state_impl = this->data(state);
        if (state_impl.data.empty()) {
            to->append_default();
            return;
        }
        to->append_datum(state_impl.data[0]);
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const final {
        // Used for streaming aggregation. Not implemented.
        DCHECK(false) << "convert_to_serialize_format is not supported";
    }

    std::string get_name() const override {
        if constexpr (is_first) {
            return "celonis_sorted_first";
        } else {
            return "celonis_sorted_last";
        }
     }
};

} // namespace starrocks
