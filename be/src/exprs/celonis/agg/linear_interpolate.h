#pragma once

#include <optional>

#include "column/column_helper.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/agg/window.h"
#include "gutil/casts.h"
#include "types/logical_type.h"

namespace starrocks {

struct CelonisLinearInterpolateState {
    double value = 0.0;                         // output for the current row; read by get_values_helper.
    bool has_value = false;                     // true once we know what value to fill with.
    int64_t anchor_row = -1;                    // row index of the most recent non-null row.
    double anchor_value = 0.0;                  // value of the most recent non-null row.
    std::optional<double> slope = std::nullopt; // slope for the current null segment.
    bool is_null = false;                       // true when the entire partition is null.

    void update_value(const double new_value) {
        value = new_value;
        has_value = true;
    }
};

template <LogicalType LT, typename T = RunTimeCppType<LT>>
class CelonisLinearInterpolateWindowFunction final
        : public ValueWindowFunction<TYPE_DOUBLE, CelonisLinearInterpolateState> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& s = this->data(state);
        s.value = 0.0;
        s.has_value = false;
        s.anchor_row = -1;
        s.anchor_value = 0.0;
        s.slope = std::nullopt;
        s.is_null = false;
    }

    void update_batch_single_state_with_frame(FunctionContext* ctx, AggDataPtr __restrict state, const Column** columns,
                                              const int64_t peer_group_start, const int64_t peer_group_end,
                                              const int64_t frame_start, const int64_t frame_end) const override {
        auto& s = this->data(state);
        if (s.is_null) {
            // Early exit: is_null is only ever set to true in the all-null branch below.
            return;
        }

        // frame is ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW, so: current_row = frame_end - 1
        const int64_t current_row = frame_end - 1;
        const auto* input_column = columns[0];
        if (!input_column->is_null(current_row)) {
            // Non-null: store the actual column value, set the anchor row/value and reset slope so the next null segment computes a fresh one.
            const auto value = read_as_double(input_column, current_row);
            s.update_value(value);
            s.anchor_value = value;
            s.anchor_row = current_row;
            s.slope = std::nullopt;
        } else if (!s.has_value) {
            // Leading null: scan ahead for the first non-null in the partition to do backward-filling.
            const int64_t next_nonnull_row = ColumnHelper::find_nonnull(input_column, frame_end, peer_group_end);
            if (next_nonnull_row < peer_group_end) {
                const auto value = read_as_double(input_column, next_nonnull_row);
                s.update_value(value);
                s.anchor_value = value;
                s.slope = 0.0; // Fill the next null-values with slope 0.0
            } else {
                // All values in the partition are null.
                s.is_null = true;
            }
        } else {
            if (!s.slope.has_value()) {
                const int64_t next_nonnull_row = ColumnHelper::find_nonnull(input_column, frame_end, peer_group_end);
                if (next_nonnull_row < peer_group_end) {
                    s.slope = (read_as_double(input_column, next_nonnull_row) - s.anchor_value) /
                              static_cast<double>(next_nonnull_row - s.anchor_row);
                } else {
                    s.slope = 0.0; // trailing nulls: forward-fill the last non-null value.
                }
            }

            const auto count_of_nulls_since_anchor_row = current_row - s.anchor_row;
            const double interpolated = s.anchor_value + count_of_nulls_since_anchor_row * s.slope.value();
            s.update_value(interpolated);
        }
    }

    void get_values(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* dst, const size_t start,
                    const size_t end) const override {
        this->get_values_helper(state, dst, start, end);
    }

    std::string get_name() const override { return "celonis_linear_interpolate"; }

private:
    // Read the value at 'row' from 'column' as a double, regardless of LT.
    static double read_as_double(const Column* column, int64_t row) {
        using ColumnType = RunTimeColumnType<LT>;
        const Column* data_column = ColumnHelper::get_data_column(column);
        const auto* col = down_cast<const ColumnType*>(data_column);
        return static_cast<double>(AggDataTypeTraits<LT>::get_row_ref(*col, row));
    }
};

} // namespace starrocks