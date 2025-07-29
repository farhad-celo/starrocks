#pragma once

#include <execution>
#include <numeric>

#include "column/datum.h"
#include "exprs/agg/percentile_cont.h"
#include "exprs/function_context.h"
#include "util/orlp/pdqsort.h"

namespace starrocks {

template<LogicalType LT>
struct TrimmedMeanParallelExecutionThreshold {
    static constexpr int64_t value = 1000;
};

/*
Benchmarking std::accumulate vs std::reduce(std::execution::par_unseq)
Running 10 iterations for each size
=== Benchmarking for int type ===
        Size accumulate (us)     reduce (us)         Speedup
------------------------------------------------------------
         100            0.00           52.10            0.00x
        1000            0.00           15.40            0.00x
       10000            0.50           25.70            0.02x
       15000            1.00           54.50            0.02x
       20000            1.00           50.10            0.02x
      100000            7.10           57.60            0.12x
      200000           15.10           64.70            0.23x
      400000           31.80           80.50            0.40x
      800000           64.10          105.70            0.61x
     1000000           80.00          118.90            0.67x
    10000000         1725.10          651.40            2.65x
*/

template<>
struct TrimmedMeanParallelExecutionThreshold<TYPE_INT> {
    static constexpr int64_t value = 10'000'000;
};

/*
=== Benchmarking for int64_t type ===
        Size accumulate (us)     reduce (us)         Speedup
------------------------------------------------------------
         100            0.00            2.10            0.00x
        1000            0.00           14.10            0.00x
       10000            6.90           36.70            0.19x
       15000            8.20           38.10            0.22x
       20000            6.00           54.20            0.11x
      100000           30.90           67.20            0.46x
      200000           65.20           74.20            0.88x
      400000          126.70          100.00            1.27x
      800000          260.60          150.40            1.73x
     1000000          316.20          168.70            1.87x
    10000000         3901.20         1132.10            3.45x
*/

template<>
struct TrimmedMeanParallelExecutionThreshold<TYPE_BIGINT> {
    static constexpr int64_t value = 400'000;
};

/*
=== Benchmarking for double type ===
        Size accumulate (us)     reduce (us)         Speedup
------------------------------------------------------------
         100            0.00            6.80            0.00x
        1000            0.00           15.30            0.00x
       10000            9.00           50.80            0.18x
       15000           13.10           50.30            0.26x
       20000           18.00           54.30            0.33x
      100000           92.00           70.10            1.31x
      200000          187.00           93.50            2.00x
      400000          374.50          114.00            3.29x
      800000          753.20          172.70            4.36x
     1000000          938.00          199.40            4.70x
    10000000         9427.80         1156.60            8.15x
*/
template<>
struct TrimmedMeanParallelExecutionThreshold<TYPE_DOUBLE> {
    static constexpr int64_t value = 100'000;
};

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
            ctx->set_error("CELONIS_TRIMMED_MEAN: Cutoff value must be in interval [0, 100].", false);
            column->append_default();
            return;
        }
        if (lower_cutoff + upper_cutoff > 100) {
            ctx->set_error(
                    "CELONIS_TRIMMED_MEAN: Sum of lower cutoff and upper cutoff must be in interval [0, 100].",
                    false);
            column->append_default();
            return;
        }

        using CppType = RunTimeCppType<LT>;

        const int64_t parallel_threshold = TrimmedMeanParallelExecutionThreshold<LT>::value;

        auto new_vector = std::move(this->data(state).items);
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
        CppType sum;
        // Use parallel execution only for large arrays (> parallel_threshold)
        const auto count = static_cast<int64_t>(last) - static_cast<int64_t>(first);
        if (count > parallel_threshold) {
            sum = std::reduce(std::execution::par_unseq,
                              new_vector.begin() + first,
                              new_vector.begin() + last);
        } else {
            // Sequential sum for small arrays to avoid parallel overhead
            sum = std::accumulate(new_vector.begin() + first,
                                  new_vector.begin() + last,
                                  static_cast<CppType>(0));
        }
        auto mean = static_cast<double>(sum) / count;
        column->append(mean);
    }

    std::string get_name() const override { return "celonis_trimmed_mean"; }
};

} // namespace starrocks
