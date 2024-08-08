#pragma once

#include "column/column_helper.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/agg/window.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "types/logical_type.h"
#include "util/phmap/btree.h"

namespace starrocks {

struct CelonisMovingCountDistinctAggregateState {
    // TODO(y.zhang): Explore the possibility of using hash map.
    phmap::btree_map<DatumKey, int64_t> frequency_map;
    bool is_frame_init = false;

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

class CelonisMovingCountDistinctAggregateFunction final
        : public WindowFunction<CelonisMovingCountDistinctAggregateState> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).frequency_map = {};
        this->data(state).is_frame_init = false;
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
        DCHECK(!ignore_subtraction);
        DCHECK(!ignore_addition);
        const auto frame_start =
                std::min(std::max(current_row_position + rows_start_offset, partition_start), partition_end);
        const auto frame_end =
                std::max(std::min(current_row_position + rows_end_offset + 1, partition_end), partition_start);
        const auto frame_size = frame_end - frame_start;
        // For cases like: rows between 2 preceding and 1 preceding
        // If frame_start ge frame_end, means the frame is empty,
        // we could directly return.
        if (frame_size <= 0) {
            return;
        }
        const int64_t previous_frame_first_position = current_row_position - 1 + rows_start_offset;
        const int64_t current_frame_last_position = current_row_position + rows_end_offset;
        if (this->data(state).is_frame_init) {
            if (previous_frame_first_position >= partition_start &&
                previous_frame_first_position < partition_end) {
                this->data(state).retract(ctx, columns, previous_frame_first_position);
            }
            if (current_frame_last_position >= partition_start &&
                current_frame_last_position < partition_end) {
                this->data(state).update(ctx, columns, current_frame_last_position);
            }
        } else {
            // Build the frame for the first time
            for (size_t i = frame_start; i < frame_end; ++i) {
                this->data(state).update(ctx, columns, i);
            }
            this->data(state).is_frame_init = true;
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

    std::string get_name() const override { return "celonis_moving_count_distinct"; }
};

} // namespace starrocks
