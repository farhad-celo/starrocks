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
2025-01-07T18:09:55+00:00
Running ./be/build_Release/src/bench/celonis/output/merge_sorted_arrays_bench
Run on (32 X 3243.06 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.17, 7.33, 5.75
// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MergeSortedArraysVARCHAR/10000/5/5/15     42039685 ns     42039078 ns           17 RowInvRate=4.20391us
BM_MergeSortedArraysVARCHAR/10000/10/5/15    87474798 ns     87471829 ns            8 RowInvRate=8.74718us
BM_MergeSortedArraysVARCHAR/10000/20/5/15   194794392 ns    194790857 ns            4 RowInvRate=19.4791us
BM_MergeSortedArraysVARCHAR/10000/30/5/15   321984796 ns    321979387 ns            2 RowInvRate=32.1979us
BM_MergeSortedArraysVARCHAR/10000/5/10/15    51357245 ns     51356480 ns           14 RowInvRate=5.13565us
BM_MergeSortedArraysVARCHAR/10000/10/10/15  111036887 ns    111035224 ns            6 RowInvRate=11.1035us
BM_MergeSortedArraysVARCHAR/10000/20/10/15  255310012 ns    255300080 ns            3 RowInvRate=25.53us
BM_MergeSortedArraysVARCHAR/10000/30/10/15  404339174 ns    404327815 ns            2 RowInvRate=40.4328us
BM_MergeSortedArraysVARCHAR/10000/5/5/30     70232036 ns     70226569 ns           10 RowInvRate=7.02266us
BM_MergeSortedArraysVARCHAR/10000/10/5/30   154122687 ns    154118156 ns            5 RowInvRate=15.4118us
BM_MergeSortedArraysVARCHAR/10000/20/5/30   347877717 ns    347873545 ns            2 RowInvRate=34.7874us
BM_MergeSortedArraysVARCHAR/10000/30/5/30   588819845 ns    588786170 ns            1 RowInvRate=58.8786us
BM_MergeSortedArraysVARCHAR/10000/5/10/30    81099871 ns     81098203 ns            9 RowInvRate=8.10982us
BM_MergeSortedArraysVARCHAR/10000/10/10/30  180131627 ns    180129314 ns            4 RowInvRate=18.0129us
BM_MergeSortedArraysVARCHAR/10000/20/10/30  414099561 ns    414099467 ns            2 RowInvRate=41.4099us
BM_MergeSortedArraysVARCHAR/10000/30/10/30  685876605 ns    685846463 ns            1 RowInvRate=68.5846us
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
        auto limit_column =
                ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
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
        ASSERT_TRUE(CelonisArrayFunctions::merge_sorted_arrays(ctx.get(),
                {input_column, timestamp_column, size_column, priority_column, secondary_order_column, limit_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}


// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
BENCHMARK(BM_MergeSortedArraysVARCHAR)->ArgsProduct({{10000}, {5, 10, 20, 30}, {5, 10}, {15, 30}});

} // namespace starrocks

BENCHMARK_MAIN();
