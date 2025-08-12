#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_functions.cpp"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
-----------------------------------------------------------------------------------------------------------------
Benchmark                                                       Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------------
// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
BM_MergeSortedArraysVARCHAR/10000/5/5/15     16636121 ns     16635391 ns           42 RowInvRate=1.66354us
BM_MergeSortedArraysVARCHAR/10000/10/5/15    35959950 ns     35956556 ns           20 RowInvRate=3.59566us
BM_MergeSortedArraysVARCHAR/10000/20/5/15    90582958 ns     90580356 ns            8 RowInvRate=9.05804us
BM_MergeSortedArraysVARCHAR/10000/30/5/15   153578283 ns    153569953 ns            5 RowInvRate=15.357us
BM_MergeSortedArraysVARCHAR/10000/5/10/15    21177941 ns     21176821 ns           32 RowInvRate=2.11768us
BM_MergeSortedArraysVARCHAR/10000/10/10/15   46872417 ns     46869892 ns           15 RowInvRate=4.68699us
BM_MergeSortedArraysVARCHAR/10000/20/10/15  121397355 ns    121393683 ns            6 RowInvRate=12.1394us
BM_MergeSortedArraysVARCHAR/10000/30/10/15  194113399 ns    194105232 ns            4 RowInvRate=19.4105us
BM_MergeSortedArraysVARCHAR/10000/5/5/30     28302196 ns     28301197 ns           25 RowInvRate=2.83012us
BM_MergeSortedArraysVARCHAR/10000/10/5/30    66333868 ns     66331954 ns           10 RowInvRate=6.6332us
BM_MergeSortedArraysVARCHAR/10000/20/5/30   163046103 ns    163035695 ns            4 RowInvRate=16.3036us
BM_MergeSortedArraysVARCHAR/10000/30/5/30   275533601 ns    275519667 ns            3 RowInvRate=27.552us
BM_MergeSortedArraysVARCHAR/10000/5/10/30    33553850 ns     33552687 ns           21 RowInvRate=3.35527us
BM_MergeSortedArraysVARCHAR/10000/10/10/30   80941968 ns     80937663 ns            9 RowInvRate=8.09377us
BM_MergeSortedArraysVARCHAR/10000/20/10/30  200011309 ns    199951632 ns            4 RowInvRate=19.9952us
BM_MergeSortedArraysVARCHAR/10000/30/10/30  315781135 ns    315759055 ns            2 RowInvRate=31.5759us

// Args: Number of rows / Number of unique elements / Max number of duplicates per unique element
BM_DedupSortedByVARCHAR/10000/5/5             1144122 ns      1144010 ns          618 RowInvRate=114.401ns
BM_DedupSortedByVARCHAR/10000/10/5            2396724 ns      2395925 ns          292 RowInvRate=239.593ns
BM_DedupSortedByVARCHAR/10000/5/10            1833664 ns      1833535 ns          381 RowInvRate=183.354ns
BM_DedupSortedByVARCHAR/10000/10/10           5544488 ns      5543776 ns          122 RowInvRate=554.378ns

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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)));

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
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            DatumArray timestamp_array;
            DatumArray size_array;
            DatumArray priority_array;
            for (int j = 0; j < num_arrays; j++) {
                int num_elements = uniform_element(rng);
                int64_t unix_timestamp = 1672560000; // 2023-01-01
                for (int k = 0; k < num_elements; k++) {
                    input_array.emplace_back(Slice(values[uniform_value(rng)]));
                    unix_timestamp += uniform_timestamp_increase(rng);
                    timestamp.from_unix_second(unix_timestamp);
                    timestamp_array.emplace_back(timestamp);
                }
                size_array.emplace_back(num_elements);
                priority_array.emplace_back(uniform_priority(rng));
            }
            input_column->append_datum(input_array);
            timestamp_column->append_datum(timestamp_array);
            size_column->append_datum(size_array);
            priority_column->append_datum(priority_array);
        }

        state.ResumeTiming();
        auto result = CelonisMergeSortedArrays::process(
                {input_column, timestamp_column, size_column, priority_column});
        ASSERT_TRUE(result.ok()) << result.status().get_error_msg();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_DedupSortedByVARCHAR(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_unique_elements = state.range(1);
    int max_duplicates = state.range(2);

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    int num_values = num_unique_elements * 10; // Some number. This doesn't matter much unless it is too small.
    UniformInt uniform_value(0, num_values - 1);
    UniformInt uniform_duplicates(1, max_duplicates);

    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto key_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            DatumArray key_array;
            for (int j = 0; j < num_unique_elements; j++) {
                Slice input = values[uniform_value(rng)];
                // To be strict, we need to generate a key different from the previous one. But it wouldn't matter much.
                Slice key = values[uniform_value(rng)];
                int num_duplicates = uniform_duplicates(rng);
                for (int k = 0; k < num_duplicates; k++) {
                    input_array.emplace_back(input);
                    key_array.emplace_back(key);
                }
            }
            input_column->append_datum(input_array);
            key_column->append_datum(key_array);
        }

        state.ResumeTiming();
        auto result = CelonisDedupSortedBy::process({input_column, key_column});
        ASSERT_TRUE(result.ok()) << result.status().get_error_msg();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
BENCHMARK(BM_MergeSortedArraysVARCHAR)->ArgsProduct({{10000}, {5, 10, 20, 30}, {5, 10}, {15, 30}});

// Args: Number of rows / Number of unique elements / Max number of duplicates per unique element
BENCHMARK(BM_DedupSortedByVARCHAR)->ArgsProduct({{10000}, {5, 10}, {5, 10}});
} // namespace starrocks

BENCHMARK_MAIN();