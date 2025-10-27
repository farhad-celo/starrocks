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

template <LogicalType LT, typename = guard::Guard>
struct TreeMultiSet {};

template <LogicalType LT>
struct TreeMultiSet<LT, FixedLengthLTGuard<LT>> {
    using CppType = RunTimeCppValueType<LT>;
    using KeyType = CppType;
    using MultiSet = phmap::btree_multiset<KeyType>;
};

template <LogicalType LT>
struct TreeMultiSet<LT, StringLTGuard<LT>> {
    using CppType = RunTimeCppValueType<LT>;
    using KeyType = std::string;
    using MultiSet = phmap::btree_multiset<KeyType>;
};

template <LogicalType LT>
struct CelonisMovingMedianAggregateState {
    using CppType = RunTimeCppType<LT>;
    using ColumnType = RunTimeColumnType<LT>;
    using MultiSet = typename TreeMultiSet<LT>::MultiSet;
    using MultiSetKeyType = typename TreeMultiSet<LT>::KeyType;

    MultiSet lower_half;
    MultiSet upper_half;
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
            auto to_add_key = _convert_to_key_type(to_add);
            if (lower_half.empty() || to_add_key <= *lower_half.rbegin()) {
                lower_half.insert(to_add_key);
            } else {
                upper_half.insert(to_add_key);
            }
            // Re-balance the halves if necessary
            if (lower_half.size() > upper_half.size() + 1) {
                upper_half.insert(*lower_half.rbegin());
                lower_half.erase(lower_half.lower_bound(*lower_half.rbegin()));
            } else if (upper_half.size() > lower_half.size()) {
                lower_half.insert(*upper_half.begin());
                upper_half.erase(upper_half.begin());
            }
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
            auto to_remove_key = _convert_to_key_type(to_remove);
            auto it_lower = lower_half.find(to_remove_key);
            if (it_lower != lower_half.end()) {
                lower_half.erase(it_lower);
            } else {
                auto it_upper = upper_half.find(to_remove_key);
                if (it_upper != upper_half.end()) {
                    upper_half.erase(it_upper);
                }
            }
        }
    }

    std::optional<CppType> get_median() const {
        if (lower_half.empty()) {
            return std::nullopt;
        }
        if (lower_half.size() > upper_half.size()) {
            return *lower_half.rbegin();
        }
        return *upper_half.begin();
    }

private:
    MultiSetKeyType _convert_to_key_type(CppType v) {
        if constexpr (lt_is_string<LT>) {
            return std::string(v.data, v.size);
        } else {
            return v;
        }
    }
};

template <LogicalType LT>
class CelonisMovingMedianAggregateFunction final : public WindowFunction<CelonisMovingMedianAggregateState<LT>> {
public:
    using CppType = RunTimeCppType<LT>;
    using ColumnType = RunTimeColumnType<LT>;

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).lower_half = {};
        this->data(state).upper_half = {};
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
        for (auto i = frame_start; i < frame_end; ++i) {
            this->data(state).update(ctx, columns, i);
        }
    }

    void update_state_removable_cumulatively(FunctionContext* ctx, AggDataPtr __restrict state, const Column** columns,
                                             int64_t current_row_position, int64_t partition_start,
                                             int64_t partition_end, int64_t rows_start_offset, int64_t rows_end_offset,
                                             bool ignore_subtraction, bool ignore_addition,
                                             [[maybe_unused]] bool has_null) const override {
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
            if (previous_frame_first_position >= partition_start && previous_frame_first_position < partition_end) {
                this->data(state).retract(ctx, columns, previous_frame_first_position);
            }
            if (current_frame_last_position >= partition_start && current_frame_last_position < partition_end) {
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
        std::optional<CppType> median = s.get_median();
        auto* nullable_dst = down_cast<NullableColumn*>(dst);
        auto* data_column = down_cast<ColumnType*>(nullable_dst->data_column().get());
        // BinaryColumn hasn't been resized, because the underlying _bytes and _offsets column couldn't be resized.
        // copied the implementation of ValueWindowFunction in window.h
        if constexpr (lt_is_string<LT>) {
            if (median.has_value()) {
                NullData& null_data = nullable_dst->null_column_data();
                for (size_t i = start; i < end; ++i) {
                    null_data.emplace_back(0);
                }
                for (size_t i = start; i < end; ++i) {
                    data_column->append(Slice(median.value()));
                }
            } else {
                nullable_dst->append_nulls(end - start);
            }
        } else {
            if (median.has_value()) {
                for (size_t i = start; i < end; ++i) {
                    data_column->get_data()[i] = median.value();
                }
            } else {
                for (size_t i = start; i < end; ++i) {
                    nullable_dst->set_null(i);
                }
            }
        }
    }

    std::string get_name() const override { return "celonis_moving_median"; }
};

} // namespace starrocks
