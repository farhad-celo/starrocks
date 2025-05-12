#pragma once

#include <execution>
#include <numeric>

#include "column/datum.h"
#include "exprs/agg/percentile_cont.h"
#include "exprs/function_context.h"
#include "util/orlp/pdqsort.h"

namespace starrocks {

/**
 * @param: [ input_column [, lower_cutoff [, upper_cutoff ]] ]
 * @paramType columns: [ BIGINT | DOUBLE [, INT [, INT]] ]
 * @return: input_array type
 * lower_cutoff (optional) : Between 0 and 100. Default is 5.
 * upper_cutoff (optional) : Between 0 and 100. Default is 5.
 * Supports PQL TRIMMED_MEAN https://confluence.celonis.com/display/PQLdevelopment/TRIMMED_MEAN
 */
template <LogicalType LT>
class CelonisTrimmedMeanAggregateFunction final : public PercentileContDiscAggregateFunction<LT> {
    using InputCppType = RunTimeCppType<LT>;
    using InputColumnType = RunTimeColumnType<LT>;
    static constexpr auto ResultLT = TYPE_DOUBLE;
    using ResultType = RunTimeCppType<ResultLT>;
    using ResultColumnType = RunTimeColumnType<ResultLT>;

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        const auto& column = down_cast<const InputColumnType&>(*columns[0]);
        this->data(state).update(column.get_data()[row_num]);
    }

    void update_batch_single_state(FunctionContext* ctx, size_t chunk_size, const Column** columns,
                                   AggDataPtr __restrict state) const override {
        const auto& column = down_cast<const InputColumnType&>(*columns[0]);
        this->data(state).update_batch(column.get_data());
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        ResultColumnType* column = down_cast<ResultColumnType*>(to);

        int lower_cutoff = 5;
        int upper_cutoff = 5;
        if (ctx->get_num_constant_columns() > 1 && ctx->is_constant_column(1)) {
            lower_cutoff = ColumnHelper::get_const_value<TYPE_INT>(ctx->get_constant_column(1));
            if (ctx->get_num_constant_columns() == 3 && ctx->is_constant_column(2)) {
                upper_cutoff = ColumnHelper::get_const_value<TYPE_INT>(ctx->get_constant_column(2));
            }
        }
        if (lower_cutoff < 0 || lower_cutoff > 100 || upper_cutoff < 0 || upper_cutoff > 100) {
            column->append(0.0);
            return;
        }

        using CppType = RunTimeCppType<LT>;
        std::vector<CppType> new_vector = std::move(this->data(state).items);
        for (auto& innerData : this->data(state).grid) {
            std::move(innerData.begin() + 1, innerData.end() - 1, std::back_inserter(new_vector));
        }
        int first = new_vector.size() * lower_cutoff / 100;
        int last = new_vector.size() - new_vector.size() * upper_cutoff / 100;
        if (first >= last) {
            column->append(0.0);
            return;
        }

        pdqsort(new_vector.begin(), new_vector.end());
        auto sum = std::reduce(std::execution::par_unseq, new_vector.begin() + first, new_vector.begin() + last);
        auto mean = static_cast<double>(sum) / (last - first);
        column->append(mean);
    }

    std::string get_name() const override { return "celonis_trimmed_mean"; }
};

} // namespace starrocks
