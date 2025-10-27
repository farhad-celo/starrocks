#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/peek_merged_sorted_arrays.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2024-12-20T11:56:12+00:00
Running ./be/build_Release/src/bench/celonis/output/peek_merged_sorted_arrays_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.04, 5.34, 6.54
// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
----------------------------------------------------------------------------------------------------------
Benchmark                                                Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------------
BM_PeekMergedSortedArraysVARCHAR/10000/5/5/15     24471406 ns     24471310 ns           29 RowInvRate=2.44713us
BM_PeekMergedSortedArraysVARCHAR/10000/10/5/15    47800036 ns     47800425 ns           15 RowInvRate=4.78004us
BM_PeekMergedSortedArraysVARCHAR/10000/20/5/15    90475502 ns     90469686 ns            8 RowInvRate=9.04697us
BM_PeekMergedSortedArraysVARCHAR/10000/30/5/15   135140706 ns    135139311 ns            5 RowInvRate=13.5139us
BM_PeekMergedSortedArraysVARCHAR/10000/5/10/15    35344727 ns     35343807 ns           20 RowInvRate=3.53438us
BM_PeekMergedSortedArraysVARCHAR/10000/10/10/15   61518564 ns     61513577 ns           11 RowInvRate=6.15136us
BM_PeekMergedSortedArraysVARCHAR/10000/20/10/15  115835149 ns    115832619 ns            6 RowInvRate=11.5833us
BM_PeekMergedSortedArraysVARCHAR/10000/30/10/15  170073455 ns    170070105 ns            4 RowInvRate=17.007us
BM_PeekMergedSortedArraysVARCHAR/10000/5/5/30     46686727 ns     46686754 ns           15 RowInvRate=4.66868us
BM_PeekMergedSortedArraysVARCHAR/10000/10/5/30    83324945 ns     83323618 ns            8 RowInvRate=8.33236us
BM_PeekMergedSortedArraysVARCHAR/10000/20/5/30   161267505 ns    161265728 ns            4 RowInvRate=16.1266us
BM_PeekMergedSortedArraysVARCHAR/10000/30/5/30   247211522 ns    247209685 ns            3 RowInvRate=24.721us
BM_PeekMergedSortedArraysVARCHAR/10000/5/10/30    52193223 ns     52191067 ns           13 RowInvRate=5.21911us
BM_PeekMergedSortedArraysVARCHAR/10000/10/10/30   94895182 ns     94892562 ns            7 RowInvRate=9.48926us
BM_PeekMergedSortedArraysVARCHAR/10000/20/10/30  180099220 ns    180088241 ns            4 RowInvRate=18.0088us
BM_PeekMergedSortedArraysVARCHAR/10000/30/10/30  294916429 ns    294916737 ns            2 RowInvRate=29.4917us
*/

static void BM_PeekMergedSortedArraysVARCHAR(benchmark::State& state) {
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
                }
                size_array.emplace_back(num_elements);
                priority_array.emplace_back(uniform_priority(rng));
            }
            input_column->append_datum(input_array);
            timestamp_column->append_datum(timestamp_array);
            size_column->append_datum(size_array);
            priority_column->append_datum(priority_array);
            secondary_order_column->append_datum(secondary_order_array);
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisPeekMergedSortedArrays<TYPE_VARCHAR>::peek_merged_sorted_arrays(
                            ctx.get(),
                            {input_column, timestamp_column, size_column, priority_column, secondary_order_column})
                            .ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
BENCHMARK(BM_PeekMergedSortedArraysVARCHAR)->ArgsProduct({{10000}, {5, 10, 20, 30}, {5, 10}, {15, 30}});

} // namespace starrocks

BENCHMARK_MAIN();
