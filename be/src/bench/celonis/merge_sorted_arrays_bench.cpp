#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-23T23:55:47+00:00
Running ./be/build_Release/src/bench/celonis/output/merge_sorted_arrays_bench
Run on (32 X 3243.38 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 11.48, 7.12, 4.26
// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MergeSortedArraysVARCHAR/10000/5/5/15     24636106 ns     24636111 ns           29 RowInvRate=2.46361us
BM_MergeSortedArraysVARCHAR/10000/10/5/15    57413020 ns     57410980 ns           12 RowInvRate=5.7411us
BM_MergeSortedArraysVARCHAR/10000/20/5/15   136972783 ns    136969692 ns            5 RowInvRate=13.697us
BM_MergeSortedArraysVARCHAR/10000/30/5/15   231952820 ns    231945830 ns            3 RowInvRate=23.1946us
BM_MergeSortedArraysVARCHAR/10000/5/10/15    32227761 ns     32227160 ns           22 RowInvRate=3.22272us
BM_MergeSortedArraysVARCHAR/10000/10/10/15   72286819 ns     72286545 ns           10 RowInvRate=7.22865us
BM_MergeSortedArraysVARCHAR/10000/20/10/15  181197482 ns    181192917 ns            4 RowInvRate=18.1193us
BM_MergeSortedArraysVARCHAR/10000/30/10/15  295198135 ns    295191525 ns            2 RowInvRate=29.5192us
BM_MergeSortedArraysVARCHAR/10000/5/5/30     43237051 ns     43234785 ns           16 RowInvRate=4.32348us
BM_MergeSortedArraysVARCHAR/10000/10/5/30    99319599 ns     99312884 ns            7 RowInvRate=9.93129us
BM_MergeSortedArraysVARCHAR/10000/20/5/30   240743337 ns    240736238 ns            3 RowInvRate=24.0736us
BM_MergeSortedArraysVARCHAR/10000/30/5/30   415479312 ns    415465439 ns            2 RowInvRate=41.5465us
BM_MergeSortedArraysVARCHAR/10000/5/10/30    50354389 ns     50351596 ns           13 RowInvRate=5.03516us
BM_MergeSortedArraysVARCHAR/10000/10/10/30  117773778 ns    117772171 ns            6 RowInvRate=11.7772us
BM_MergeSortedArraysVARCHAR/10000/20/10/30  293901402 ns    293884278 ns            2 RowInvRate=29.3884us
BM_MergeSortedArraysVARCHAR/10000/30/10/30  480462049 ns    480452847 ns            2 RowInvRate=48.0453us
*/

static void BM_MergeSortedArraysVARCHAR(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_arrays = state.range(1);
    int min_elements = state.range(2);
    int max_elements = state.range(3);

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    int num_values = num_arrays * min_elements; // Some number. This doesn't matter much.
    UniformInt uniform_value(0, num_values - 1);
    UniformInt uniform_element(min_elements, max_elements - 1);
    UniformInt uniform_timestamp_increase(1, 100);
    UniformInt uniform_priority(0, num_arrays * 10 - 1); // Some number. This doesn't matter much.

    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    TimestampValue timestamp;
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto timestamp_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), true);
        auto size_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), true);
        auto priority_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), true);
        auto secondary_order_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), true);
        auto limit_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        limit_column->append_datum(1000000L); // Currently this is the value used in PQL2SQL
        limit_column = ConstColumn::create(limit_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            DatumArray timestamp_array;
            DatumArray size_array;
            DatumArray priority_array;
            DatumArray secondary_order_array;
            for (int j = 0; j < num_arrays; j++) {
                int num_elements = uniform_element(rng);
                int64_t unix_timestamp = 1672560000; // 2023-01-01
                for (int k = 0; k < num_elements; k++) {
                    input_array.emplace_back(Slice(values[uniform_value(rng)]));
                    unix_timestamp += uniform_timestamp_increase(rng);
                    timestamp.from_unix_second(unix_timestamp);
                    timestamp_array.emplace_back(timestamp);
                    secondary_order_array.emplace_back(1);
                    priority_array.emplace_back(uniform_priority(rng));
                }
                size_array.emplace_back(num_elements);
            }
            input_column->append_datum(input_array);
            timestamp_column->append_datum(timestamp_array);
            size_column->append_datum(size_array);
            priority_column->append_datum(priority_array);
            secondary_order_column->append_datum(secondary_order_array);
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisArrayFunctions::merge_sorted_arrays(
                            ctx.get(), {input_column, timestamp_column, size_column, priority_column,
                                        secondary_order_column, limit_column})
                            .ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
BENCHMARK(BM_MergeSortedArraysVARCHAR)->ArgsProduct({{10000}, {5, 10, 20, 30}, {5, 10}, {15, 30}});

} // namespace starrocks

BENCHMARK_MAIN();
