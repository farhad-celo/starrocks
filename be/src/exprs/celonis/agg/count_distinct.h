#pragma once

#include "column/column_helper.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/agg/window.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "types/logical_type.h"

namespace starrocks {

struct CelonisCountDistinctAggregateState {
    std::map<DatumKey, int64_t> frequency_map;

    void update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        const Column* value_column = columns[0];
        if (!value_column->is_nullable() || !value_column->is_null(row_num)) {
            const DatumKey datum_key = value_column->get(row_num).convert2DatumKey();
            ++frequency_map[datum_key];
        }
    }

    void retract(FunctionContext* ctx, const Column** columns, size_t row_num) {
        const Column* value_column = columns[0];
        if (!value_column->is_nullable() || !value_column->is_null(row_num)) {
            const DatumKey datum_key = value_column->get(row_num).convert2DatumKey();
            if (--frequency_map[datum_key] == 0) {
                frequency_map.erase(datum_key);
            }
        }
    }
};

class CelonisCountDistinctAggregateFunction final
        : public WindowFunction<CelonisCountDistinctAggregateState> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).frequency_map = {};
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        this->data(state).update(ctx, columns, row_num);
    }

    void retract(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                 size_t row_num) const override {
        this->data(state).retract(ctx, columns, row_num);
    }

    void update_batch_single_state_with_frame(FunctionContext* ctx, AggDataPtr __restrict state, const Column** columns,
                                              int64_t peer_group_start, int64_t peer_group_end, int64_t frame_start,
                                              int64_t frame_end) const override {
        for (size_t i = frame_start; i < frame_end; ++i) {
            this->data(state).update(ctx, columns, i);
        }
    }

    void update_state_removable_cumulatively(FunctionContext* ctx, AggDataPtr __restrict state, const Column** columns,
                                             int64_t current_row_position, int64_t partition_start,
                                             int64_t partition_end, int64_t rows_start_offset, int64_t rows_end_offset,
                                             bool ignore_subtraction, bool ignore_addition) const override {
        const int64_t previous_frame_first_position = current_row_position - 1 + rows_start_offset;
        const int64_t current_frame_last_position = current_row_position + rows_end_offset;
        if (!ignore_subtraction && previous_frame_first_position >= partition_start &&
            previous_frame_first_position < partition_end) {
            this->data(state).retract(ctx, columns, previous_frame_first_position);
        }
        if (!ignore_addition && current_frame_last_position >= partition_start &&
            current_frame_last_position < partition_end) {
            this->data(state).update(ctx, columns, current_frame_last_position);
        }
    }

    void get_values(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* dst, size_t start,
                    size_t end) const override {
        DCHECK_GT(end, start);
        auto& s = this->data(state);
        if (dst->is_nullable()) {
            auto* nullable_dst = down_cast<NullableColumn*>(dst);
            auto* data_column = down_cast<Int64Column*>(nullable_dst->data_column().get());
            for (size_t i = start; i < end; ++i) {
                data_column->get_data()[i] = s.frequency_map.size();
            }
        } else {
            auto* data_column = down_cast<Int64Column*>(dst);
            for (size_t i = start; i < end; ++i) {
                data_column->get_data()[i] = s.frequency_map.size();
            }
        }
    }

    std::string get_name() const override { return "celonis_count_distinct"; }
};

} // namespace starrocks
