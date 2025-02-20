#pragma once

#include <stdexcept>
#include <string>

#include "column/column_helper.h"
#include "column/datum.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/agg/window.h"
#include "gutil/casts.h"
#include "types/logical_type.h"

namespace starrocks {

// TODO(l.karnowski) instead of Datum, we could type this to the actual array type.
struct CelonisArrayLeadLagAggregateState {
    /**
     * Vector of size k which keeps track of all seen non-null elements in the frame. Initially the vector
     * contains k NULL values. One by one, non-NULL elements are added via the add() function. Once the vector
     * contains k non-NULL values, more non-NULL values are added in a round-robin fashion.
     */
    DatumArray last_k_non_null{};
    /**
     * The index points to the oldest non-NULL value.
     */
    int64_t current_kth_element{};
    /**
     * The output row that is computed in update() and appended to the result column in get_values()
     */
    Datum current_row{};

    [[nodiscard]] const Datum& get_kth_non_null_element() const {
        return last_k_non_null.at(current_kth_element);
    }

    [[nodiscard]] bool not_enough_not_null_elements() const {
        return last_k_non_null.back().is_null();
    }

    void add(const Datum& datum) {
        if (datum.is_null()) {
            return;
        }

        last_k_non_null.at(current_kth_element) = datum;
        ++current_kth_element;
        if (current_kth_element == last_k_non_null.size()) {
            current_kth_element = 0;
        }
    }
};

// TODO(l.karnowski) Implement LEAD as well, right now only LAG is supported
class CelonisArrayLeadLagAggregateFunction final : public WindowFunction<CelonisArrayLeadLagAggregateState> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& state_impl = this->data(state);
        // get offset
        DCHECK(args.size() == 2);
        const Column* arg1 = args[1].get();
        DCHECK(arg1->is_constant());
        const int64_t k = ColumnHelper::get_const_value<LogicalType::TYPE_BIGINT>(arg1);
        if (k < 1) {
            throw std::runtime_error{fmt::format("{}: k must be a positive integer", get_name())};
        }
        state_impl.last_k_non_null = DatumArray(k, kNullDatum);
        state_impl.current_kth_element = 0;
        state_impl.current_row = {};
    }

    void update_batch_single_state_with_frame(FunctionContext* ctx, AggDataPtr __restrict state, const Column** columns,
                                              int64_t peer_group_start, int64_t peer_group_end, int64_t frame_start,
                                              int64_t frame_end) const override {
        auto& state_impl = this->data(state);

        const auto* input = columns[0];
        DCHECK_EQ(frame_start + 1, frame_end);
        const Datum current_row = input->get(frame_start);

        if (current_row.is_null()) {
            state_impl.current_row = kNullDatum;
            return;
        } 

        const auto& current_array = current_row.get_array();

        DatumArray out = DatumArray{};
        out.resize(current_array.size());

        // Write into output and keep updating the last k non-null values
        for(size_t idx = 0; idx < out.size(); ++idx) {
            if (state_impl.not_enough_not_null_elements()) {
                // As long as we have not collected enough non-null values, output NULLs
                out.at(idx) = kNullDatum;
            } else {
                out.at(idx) = state_impl.get_kth_non_null_element();
            }
            state_impl.add(current_array.at(idx));
        }

        state_impl.current_row = out;
    }

    void get_values(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* dst, size_t start,
                    size_t end) const override {
        const auto& state_impl = this->data(state);
        DCHECK_EQ(start + 1, end);
        // append is used since array columns are only reserved (and not resized) in analytor.cpp
        dst->append_datum(state_impl.current_row);
    }

    std::string get_name() const override { return "celonis_array_lead_lag_window"; }
};

} // namespace starrocks
