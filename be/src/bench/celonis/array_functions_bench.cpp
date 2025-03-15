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
2025-03-14T15:37:43+00:00
Running ./be/build_Release/src/bench/celonis/output/array_functions_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.74, 3.94, 3.23
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
// Args: Number of rows / Number of unique elements in the array / Max number of duplicates per unique element
BM_DedupSortedByVARCHAR/10000/10/5     2428011 ns      2427770 ns          291 RowInvRate=242.777ns
BM_DedupSortedByVARCHAR/10000/20/5     6283689 ns      6282785 ns          111 RowInvRate=628.279ns
BM_DedupSortedByVARCHAR/10000/10/10    4334589 ns      4334238 ns          161 RowInvRate=433.424ns
BM_DedupSortedByVARCHAR/10000/20/10    9249412 ns      9247639 ns           76 RowInvRate=924.764ns
// Args: Number of rows / True percentage / Array length
BM_ArrayBoolOr/1000/1/10                 14633 ns        14579 ns        47432 RowInvRate=14.5794ns
BM_ArrayBoolOr/10000/1/10               139060 ns       138951 ns         5257 RowInvRate=13.8951ns
BM_ArrayBoolOr/1000/10/10                19303 ns        19249 ns        36953 RowInvRate=19.2493ns
BM_ArrayBoolOr/10000/10/10              190497 ns       187573 ns         3789 RowInvRate=18.7573ns
BM_ArrayBoolOr/1000/50/10                20257 ns        20180 ns        32987 RowInvRate=20.1795ns
BM_ArrayBoolOr/10000/50/10              192363 ns       192259 ns         3855 RowInvRate=19.2259ns
BM_ArrayBoolOr/1000/1/20                 18957 ns        18899 ns        37428 RowInvRate=18.8988ns
BM_ArrayBoolOr/10000/1/20               184758 ns       184632 ns         3993 RowInvRate=18.4632ns
BM_ArrayBoolOr/1000/10/20                21940 ns        21881 ns        32065 RowInvRate=21.8807ns
BM_ArrayBoolOr/10000/10/20              206817 ns       206707 ns         3340 RowInvRate=20.6707ns
BM_ArrayBoolOr/1000/50/20                20661 ns        20605 ns        35024 RowInvRate=20.6053ns
BM_ArrayBoolOr/10000/50/20              182548 ns       182440 ns         3713 RowInvRate=18.244ns
// Args: Number of rows / Null percentage / Array length
BM_ArrayCountVarchar/1000/0/20           10837 ns        10733 ns        65169 RowInvRate=10.7332ns
BM_ArrayCountVarchar/10000/0/20          85396 ns        85252 ns        12728 RowInvRate=8.52516ns
BM_ArrayCountVarchar/1000/10/20          12406 ns        12295 ns        53709 RowInvRate=12.295ns
BM_ArrayCountVarchar/10000/10/20        106069 ns       105943 ns         6698 RowInvRate=10.5943ns
BM_ArrayCountVarchar/1000/50/20          13110 ns        13011 ns        56816 RowInvRate=13.0115ns
BM_ArrayCountVarchar/10000/50/20        106957 ns       106796 ns         7112 RowInvRate=10.6796ns
BM_ArrayCountVarchar/1000/0/100           8145 ns         8004 ns        63514 RowInvRate=8.00351ns
BM_ArrayCountVarchar/10000/0/100        126271 ns       125878 ns         5565 RowInvRate=12.5878ns
BM_ArrayCountVarchar/1000/10/100         30326 ns        30179 ns        21304 RowInvRate=30.1792ns
BM_ArrayCountVarchar/10000/10/100       335791 ns       335445 ns         2097 RowInvRate=33.5445ns
BM_ArrayCountVarchar/1000/50/100         30525 ns        30333 ns        21192 RowInvRate=30.3333ns
BM_ArrayCountVarchar/10000/50/100       306140 ns       305998 ns         2288 RowInvRate=30.5998ns
*/

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
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ArrayBoolOr(benchmark::State& state) {
    int num_rows = state.range(0);
    double probability = state.range(1) / 100.0;
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

        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ArrayCountVarchar(benchmark::State& state) {
    int num_rows = state.range(0);
    double null_probability = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(null_probability);

    std::vector<std::string> strings;
    strings.reserve(array_length);
    for (int j = 0; j < array_length; ++j) {
        strings.emplace_back("value" + std::to_string(j));
    }
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
                    input_array.emplace_back(Slice(strings[j]));
                }
            }
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = CelonisArrayFunctions::array_count(ctx.get(), {input_column});

        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of unique elements in the array / Max number of duplicates per unique element
BENCHMARK(BM_DedupSortedByVARCHAR)->ArgsProduct({{10000}, {10, 20}, {5, 10}});

// Args: Number of rows / True percentage / Array length
BENCHMARK(BM_ArrayBoolOr)->ArgsProduct({{1000, 10000}, {1, 10, 50}, {10, 20}});

// Args: Number of rows / Null percentage / Array length
BENCHMARK(BM_ArrayCountVarchar)->ArgsProduct({{1000, 10000}, {0, 10, 50}, {20, 100}});

} // namespace starrocks

BENCHMARK_MAIN();
