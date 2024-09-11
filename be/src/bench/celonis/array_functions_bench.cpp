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
2024-09-11T14:11:52+00:00
Running ./be/build_Release/src/bench/celonis/output/array_functions_bench
Run on (32 X 2445.42 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.49, 0.59, 0.36
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MergeSortedArraysVARCHAR/10000/5/5/15     30274727 ns     30271822 ns           23 RowInvRate=3.02718us
BM_MergeSortedArraysVARCHAR/10000/10/5/15    66993149 ns     66986510 ns           10 RowInvRate=6.69865us
BM_MergeSortedArraysVARCHAR/10000/20/5/15   154471575 ns    154461676 ns            5 RowInvRate=15.4462us
BM_MergeSortedArraysVARCHAR/10000/30/5/15   256502281 ns    256480289 ns            3 RowInvRate=25.648us
BM_MergeSortedArraysVARCHAR/10000/5/10/15    36959726 ns     36955486 ns           19 RowInvRate=3.69555us
BM_MergeSortedArraysVARCHAR/10000/10/10/15   82913384 ns     82910422 ns            8 RowInvRate=8.29104us
BM_MergeSortedArraysVARCHAR/10000/20/10/15  199408291 ns    199404237 ns            4 RowInvRate=19.9404us
BM_MergeSortedArraysVARCHAR/10000/30/10/15  315578735 ns    315561648 ns            2 RowInvRate=31.5562us
BM_MergeSortedArraysVARCHAR/10000/5/5/30     47109811 ns     47109102 ns           15 RowInvRate=4.71091us
BM_MergeSortedArraysVARCHAR/10000/10/5/30   108553692 ns    108539168 ns            6 RowInvRate=10.8539us
BM_MergeSortedArraysVARCHAR/10000/20/5/30   259084935 ns    259073816 ns            3 RowInvRate=25.9074us
BM_MergeSortedArraysVARCHAR/10000/30/5/30   434000689 ns    433946350 ns            2 RowInvRate=43.3946us
BM_MergeSortedArraysVARCHAR/10000/5/10/30    54278889 ns     54273074 ns           12 RowInvRate=5.42731us
BM_MergeSortedArraysVARCHAR/10000/10/10/30  127947671 ns    127946689 ns            5 RowInvRate=12.7947us
BM_MergeSortedArraysVARCHAR/10000/20/10/30  310458411 ns    310436073 ns            2 RowInvRate=31.0436us
BM_MergeSortedArraysVARCHAR/10000/30/10/30  501227823 ns    501193049 ns            2 RowInvRate=50.1193us
BM_DedupSortedByVARCHAR/10000/5/5             1283851 ns      1283690 ns          577 RowInvRate=128.369ns
BM_DedupSortedByVARCHAR/10000/10/5            2472645 ns      2472358 ns          288 RowInvRate=247.236ns
BM_DedupSortedByVARCHAR/10000/5/10            1793676 ns      1793378 ns          376 RowInvRate=179.338ns
BM_DedupSortedByVARCHAR/10000/10/10           4298807 ns      4297674 ns          163 RowInvRate=429.767ns
BM_ArrayBoolOr/1000/1/10                         9663 ns         9589 ns        72937 RowInvRate=9.58935ns
BM_ArrayBoolOr/10000/1/10                       90758 ns        90622 ns         7677 RowInvRate=9.06225ns
BM_ArrayBoolOr/1000/10/10                        9629 ns         9558 ns        73277 RowInvRate=9.55847ns
BM_ArrayBoolOr/10000/10/10                      91137 ns        91015 ns         7689 RowInvRate=9.10153ns
BM_ArrayBoolOr/1000/50/10                        9671 ns         9602 ns        73008 RowInvRate=9.60245ns
BM_ArrayBoolOr/10000/50/10                      90226 ns        90089 ns         7687 RowInvRate=9.00894ns
BM_ArrayBoolOr/1000/1/20                         9852 ns         9775 ns        71616 RowInvRate=9.77516ns
BM_ArrayBoolOr/10000/1/20                       91316 ns        91206 ns         7740 RowInvRate=9.1206ns
BM_ArrayBoolOr/1000/10/20                        9775 ns         9699 ns        71906 RowInvRate=9.69863ns
BM_ArrayBoolOr/10000/10/20                      91009 ns        90907 ns         7654 RowInvRate=9.09073ns
BM_ArrayBoolOr/1000/50/20                        9778 ns         9703 ns        72245 RowInvRate=9.70329ns
BM_ArrayBoolOr/10000/50/20                      91774 ns        91654 ns         7746 RowInvRate=9.16537ns
BM_ArrayCountVarchar/1000/0/20                  10189 ns        10018 ns        70563 RowInvRate=10.0181ns
BM_ArrayCountVarchar/10000/0/20                 76125 ns        75995 ns         9177 RowInvRate=7.5995ns
BM_ArrayCountVarchar/1000/10/20                 12395 ns        12322 ns        56389 RowInvRate=12.3221ns
BM_ArrayCountVarchar/10000/10/20               107219 ns       107139 ns         6548 RowInvRate=10.7139ns
BM_ArrayCountVarchar/1000/50/20                 12383 ns        12314 ns        56806 RowInvRate=12.3136ns
BM_ArrayCountVarchar/10000/50/20               106852 ns       106777 ns         6532 RowInvRate=10.6777ns
BM_ArrayCountVarchar/1000/0/100                 10398 ns        10196 ns        68845 RowInvRate=10.1963ns
BM_ArrayCountVarchar/10000/0/100               153107 ns       152553 ns         4843 RowInvRate=15.2553ns
BM_ArrayCountVarchar/1000/10/100                32832 ns        32514 ns        22831 RowInvRate=32.5138ns
BM_ArrayCountVarchar/10000/10/100              284150 ns       284011 ns         2456 RowInvRate=28.4011ns
BM_ArrayCountVarchar/1000/50/100                32918 ns        32547 ns        22675 RowInvRate=32.5474ns
BM_ArrayCountVarchar/10000/50/100              300441 ns       299217 ns         2367 RowInvRate=29.9217ns
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

static void BM_ArrayCountVarchar(benchmark::State& state) {
    int num_rows = state.range(0);
    bool null_probability = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(null_probability);

    int total_rows = 0;
    for (auto _: state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            for (int j = 0; j < array_length; j++) {
                bool is_null = dist(gen);
                if (is_null) {
                    input_array.emplace_back(kNullDatum);
                } else {
                    input_array.emplace_back(Slice("value" + std::to_string(i)));
                }
            }
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = CelonisArrayFunctions::array_count(ctx.get(), {input_column});

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

// Args: Number of rows / Null percentage / Array length
BENCHMARK(BM_ArrayCountVarchar)->ArgsProduct({{1000, 10000}, {0, 10, 50}, {20, 100}});

} // namespace starrocks

BENCHMARK_MAIN();
