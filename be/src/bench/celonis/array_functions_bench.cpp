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
2024-09-10T18:17:07+00:00
Running ./be/build_Release/src/bench/celonis/output/array_functions_bench
Run on (32 X 2445.42 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.45, 2.60, 2.54
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MergeSortedArraysVARCHAR/10000/5/5/15     29895865 ns     29895666 ns           23 RowInvRate=2.98957us
BM_MergeSortedArraysVARCHAR/10000/10/5/15    65203523 ns     65198404 ns           11 RowInvRate=6.51984us
BM_MergeSortedArraysVARCHAR/10000/20/5/15   153271899 ns    153247447 ns            5 RowInvRate=15.3247us
BM_MergeSortedArraysVARCHAR/10000/30/5/15   252428850 ns    252429911 ns            3 RowInvRate=25.243us
BM_MergeSortedArraysVARCHAR/10000/5/10/15    36730265 ns     36729305 ns           19 RowInvRate=3.67293us
BM_MergeSortedArraysVARCHAR/10000/10/10/15   82790435 ns     82790756 ns            8 RowInvRate=8.27908us
BM_MergeSortedArraysVARCHAR/10000/20/10/15  198173962 ns    198168523 ns            4 RowInvRate=19.8169us
BM_MergeSortedArraysVARCHAR/10000/30/10/15  313915864 ns    313899504 ns            2 RowInvRate=31.39us
BM_MergeSortedArraysVARCHAR/10000/5/5/30     46942489 ns     46942041 ns           15 RowInvRate=4.6942us
BM_MergeSortedArraysVARCHAR/10000/10/5/30   107647206 ns    107644283 ns            6 RowInvRate=10.7644us
BM_MergeSortedArraysVARCHAR/10000/20/5/30   255415161 ns    255405648 ns            3 RowInvRate=25.5406us
BM_MergeSortedArraysVARCHAR/10000/30/5/30   433318432 ns    433292328 ns            2 RowInvRate=43.3292us
BM_MergeSortedArraysVARCHAR/10000/5/10/30    54129188 ns     54126965 ns           13 RowInvRate=5.4127us
BM_MergeSortedArraysVARCHAR/10000/10/10/30  126197221 ns    126191596 ns            6 RowInvRate=12.6192us
BM_MergeSortedArraysVARCHAR/10000/20/10/30  308298382 ns    308284459 ns            2 RowInvRate=30.8284us
BM_MergeSortedArraysVARCHAR/10000/30/10/30  502463786 ns    502405779 ns            1 RowInvRate=50.2406us
BM_DedupSortedByVARCHAR/10000/5/5             1203026 ns      1202818 ns          595 RowInvRate=120.282ns
BM_DedupSortedByVARCHAR/10000/10/5            2357028 ns      2355719 ns          300 RowInvRate=235.572ns
BM_DedupSortedByVARCHAR/10000/5/10            1705175 ns      1704713 ns          413 RowInvRate=170.471ns
BM_DedupSortedByVARCHAR/10000/10/10           4116546 ns      4115834 ns          168 RowInvRate=411.583ns
BM_ArrayBoolOr/1000/1/10                         9664 ns         9594 ns        72109 RowInvRate=9.59415ns
BM_ArrayBoolOr/10000/1/10                       85976 ns        85746 ns         8147 RowInvRate=8.57461ns
BM_ArrayBoolOr/1000/10/10                        9754 ns         9672 ns        72358 RowInvRate=9.67204ns
BM_ArrayBoolOr/10000/10/10                      85200 ns        84987 ns         8333 RowInvRate=8.49868ns
BM_ArrayBoolOr/1000/50/10                        9624 ns         9563 ns        72443 RowInvRate=9.56305ns
BM_ArrayBoolOr/10000/50/10                      85542 ns        85334 ns         8242 RowInvRate=8.53344ns
BM_ArrayBoolOr/1000/1/20                         9936 ns         9833 ns        72616 RowInvRate=9.83323ns
BM_ArrayBoolOr/10000/1/20                       85698 ns        85528 ns         8139 RowInvRate=8.55283ns
BM_ArrayBoolOr/1000/10/20                        9868 ns         9780 ns        71009 RowInvRate=9.78042ns
BM_ArrayBoolOr/10000/10/20                      85542 ns        85377 ns         8229 RowInvRate=8.53771ns
BM_ArrayBoolOr/1000/50/20                        9872 ns         9776 ns        72249 RowInvRate=9.77591ns
BM_ArrayBoolOr/10000/50/20                      85930 ns        85724 ns         8210 RowInvRate=8.5724ns
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

static void BM_ArrayBoolOr(benchmark::State& state) {
    int num_rows = state.range(0);
    bool probability = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BOOLEAN)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BOOLEAN));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(probability);

    int total_rows = 0;
    for (auto _: state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BOOLEAN)), true);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            for (int j = 0; j < array_length; j++) {
                input_array.emplace_back(dist(gen));
            }
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = CelonisArrayFunctions::array_bool_or(ctx.get(), {input_column});

        ASSERT_TRUE(result.ok()) << result.status().get_error_msg();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of arrays / Minimum size of inner array / Maximum size of inner array
BENCHMARK(BM_MergeSortedArraysVARCHAR)->ArgsProduct({{10000}, {5, 10, 20, 30}, {5, 10}, {15, 30}});

// Args: Number of rows / Number of unique elements / Max number of duplicates per unique element
BENCHMARK(BM_DedupSortedByVARCHAR)->ArgsProduct({{10000}, {5, 10}, {5, 10}});

// Args: Number of rows / True percentage / Array length
BENCHMARK(BM_ArrayBoolOr)->ArgsProduct({{1000, 10000}, {1, 10, 50}, {10, 20}});
} // namespace starrocks

BENCHMARK_MAIN();
