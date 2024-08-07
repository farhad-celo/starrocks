#pragma once

#include "column/column_helper.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/agg/window.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "types/logical_type.h"

namespace starrocks {

static const double DEFAULT_ONE_END_CUTOFF = 5.0;

template<LogicalType LT>
struct CelonisMovingTrimmedMeanAggregateState {
    using CppType = RunTimeCppType<LT>;
    using ColumnType = RunTimeColumnType<LT>;

    std::multiset<CppType> window;
    CppType sum = {};
    bool is_frame_init = false;

    void update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        const Column* value_column = columns[0];
        if (!value_column->is_nullable() || !value_column->is_null(row_num)) {
            CppType to_add;
            if (value_column->is_nullable()) {
                const auto* nullable_column = down_cast<const NullableColumn*>(value_column);
                const auto* data_column = down_cast<const ColumnType*>(nullable_column->data_column().get());
                to_add = data_column->get_data()[row_num];
            } else {
                const auto* column = down_cast<const ColumnType*>(value_column);
                to_add = column->get_data()[row_num];
            }
            window.insert(to_add);
            sum += to_add;
        }
    }

    void retract(FunctionContext* ctx, const Column** columns, size_t row_num) {
        const Column* value_column = columns[0];
        if (!value_column->is_nullable() || !value_column->is_null(row_num)) {
            CppType to_remove;
            if (value_column->is_nullable()) {
                const auto* nullable_column = down_cast<const NullableColumn*>(value_column);
                const auto* data_column = down_cast<const ColumnType*>(nullable_column->data_column().get());
                to_remove = data_column->get_data()[row_num];
            } else {
                const auto* column = down_cast<const ColumnType*>(value_column);
                to_remove = column->get_data()[row_num];
            }
            auto it = window.find(to_remove);
            if (it != window.end()) {
                window.erase(it);
                sum -= to_remove;
            }
        }
    }

    // TODO(y.zhang): improve the implementation.
    // Calculates the trimmed mean with a cut off of 10% for each window.
    std::optional<double> get_trimmed_mean() const {
        if (window.empty()) {
            return std::nullopt;
        }
        const auto num_trimmed = static_cast<size_t>(std::round(window.size() * DEFAULT_ONE_END_CUTOFF / 100.0));
        if (num_trimmed == 0) {
            return static_cast<double>(sum) / window.size();
        }
        DCHECK(num_trimmed <= window.size() / 2);
        double total = static_cast<double>(sum);
        auto low_it = window.begin();
        for (auto i = 0; i < num_trimmed; ++low_it, ++i) {
            total -= *low_it;
        }
        auto high_it = window.rbegin();
        for (auto i = 0; i < num_trimmed; ++high_it, ++i) {
            total -= *high_it;
        }
        return total / (window.size() - 2 * num_trimmed);
    }
};

template<LogicalType LT>
class CelonisMovingTrimmedMeanAggregateFunction final
        : public WindowFunction<CelonisMovingTrimmedMeanAggregateState<LT>> {
public:
    using CppType = RunTimeCppType<LT>;

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).window = {};
        this->data(state).sum = {};
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
        DCHECK(dst->is_nullable());
        auto& s = this->data(state);
        std::optional<double> mean = s.get_trimmed_mean();
        auto* nullable_dst = down_cast<NullableColumn*>(dst);
        auto* data_column = down_cast<DoubleColumn*>(nullable_dst->data_column().get());
        if (mean.has_value()) {
            for (size_t i = start; i < end; ++i) {
                data_column->get_data()[i] = mean.value();
            }
        } else {
            for (size_t i = start; i < end; ++i) {
                nullable_dst->set_null(i);
            }
        }
    }

    std::string get_name() const override { return "celonis_moving_trimmed_mean"; }
};

} // namespace starrocks
