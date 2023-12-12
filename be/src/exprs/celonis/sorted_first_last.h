#pragma once

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"

namespace starrocks {

template <bool is_first>
struct CelonisSortedFirstLastAggregateState {
    void set_new_data_columns(const Column** columns, size_t row_num) {
        for (int i = 0; i < data_columns->size(); ++i) {
            data_columns->at(i)->resize(0);
            data_columns->at(i)->append_datum(columns[i]->get(row_num));
        }
    }

    void update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        if (data_columns->at(0)->empty()) {
            set_new_data_columns(columns, row_num);
            return;
        }
        const auto& is_asc_order = ctx->get_is_asc_order();
        const auto& null_firsts = ctx->get_nulls_first();
        for (int i = 1; i < data_columns->size(); ++i) {
            auto order_index = i - 1;
            int nan_direction_hint = (is_asc_order[order_index] == null_firsts[order_index])
                                     ? -1 // None is considered less than everything other.
                                     : 1;
            auto cmp = data_columns->at(i)->compare_at(0, row_num, *columns[i], nan_direction_hint);
            if (cmp == 0) {
                continue;
            } else if ((cmp < 0) == is_asc_order[order_index]) {
                if constexpr (!is_first) {
                    set_new_data_columns(columns, row_num);
                }
                return;
            } else {
                if constexpr (is_first) {
                    set_new_data_columns(columns, row_num);
                }
                return;
            }
        }
    }

    ~CelonisSortedFirstLastAggregateState() {
        if (data_columns != nullptr) {
            for (auto& col : *data_columns) {
                col.reset();
            }
            data_columns->clear();
            data_columns.reset(nullptr);
        }
    }
    // using pointer rather than vector to avoid variadic size
    // celonis_sorted_first(a order by b, c, d), the a,b,c,d are put into data_columns in order.
    std::unique_ptr<Columns> data_columns = nullptr;
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
    void create(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        auto num = ctx->get_num_args();
        auto* state = new (ptr) CelonisSortedFirstLastAggregateState<is_first>;
        state->data_columns = std::make_unique<Columns>();
        for (auto i = 0; i < num; ++i) {
            state->data_columns->emplace_back(ctx->create_column(*ctx->get_arg_type(i), true));
        }
        DCHECK_EQ(state->data_columns->size(), ctx->get_is_asc_order().size() + 1);
    }

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& state_impl = this->data(state);
        if (state_impl.data_columns != nullptr) {
            for (auto& col : *state_impl.data_columns) {
                col->resize(0);
            }
        }
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        if (columns[0]->is_null(row_num)) return;
        this->data(state).update(ctx, columns, row_num);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
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
        if ((*state_impl.data_columns)[0]->empty()) {
            to->append_default();
            return;
        }
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        for (auto i = 0; i < columns.size(); ++i) {
            columns[i]->append_datum((*state_impl.data_columns)[i]->get(0));
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& state_impl = this->data(state);
        if ((*state_impl.data_columns)[0]->empty()) {
            to->append_default();
            return;
        }
        to->append_datum((*state_impl.data_columns)[0]->get(0));
    }

    // convert each cell of a row to a field in a struct
    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        auto columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(dst->get()))->fields_column();
        if (dst->get()->is_nullable()) {
            for (size_t i = 0; i < chunk_size; i++) {
                down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
            }
        }
        for (auto j = 0; j < columns.size(); ++j) {
            for (size_t i = 0; i < chunk_size; i++) {
                columns[j]->append_datum(src[j]->get(i));
            }
        }
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
