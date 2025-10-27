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
2025-03-27T11:38:31+00:00
Running ./be/build_Release/src/bench/celonis/output/array_functions_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.91, 4.39, 4.18
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
// Args: Number of rows / Number of unique elements in the array / Max number of duplicates per unique element
BM_DedupSortedByVARCHAR/10000/10/5     2454653 ns      2454393 ns          290 RowInvRate=245.439ns
BM_DedupSortedByVARCHAR/10000/20/5     6276774 ns      6276287 ns          111 RowInvRate=627.629ns
BM_DedupSortedByVARCHAR/10000/10/10    4327619 ns      4327285 ns          162 RowInvRate=432.729ns
BM_DedupSortedByVARCHAR/10000/20/10    9201218 ns      9199696 ns           76 RowInvRate=919.97ns
// Args: Number of rows / True percentage / Array length
BM_ArrayBoolOr/1000/1/10                 13628 ns        13580 ns        49883 RowInvRate=13.5805ns
BM_ArrayBoolOr/10000/1/10               124194 ns       124100 ns         5695 RowInvRate=12.41ns
BM_ArrayBoolOr/1000/10/10                18644 ns        18580 ns        38142 RowInvRate=18.5795ns
BM_ArrayBoolOr/10000/10/10              173107 ns       173010 ns         4045 RowInvRate=17.301ns
BM_ArrayBoolOr/1000/50/10                18697 ns        18648 ns        37477 RowInvRate=18.6479ns
BM_ArrayBoolOr/10000/50/10              176191 ns       176111 ns         3991 RowInvRate=17.6111ns
BM_ArrayBoolOr/1000/1/20                 17421 ns        17381 ns        39864 RowInvRate=17.3808ns
BM_ArrayBoolOr/10000/1/20               169161 ns       169065 ns         4163 RowInvRate=16.9065ns
BM_ArrayBoolOr/1000/10/20                21748 ns        21701 ns        32194 RowInvRate=21.7013ns
BM_ArrayBoolOr/10000/10/20              204236 ns       204142 ns         3430 RowInvRate=20.4142ns
BM_ArrayBoolOr/1000/50/20                18872 ns        18824 ns        37167 RowInvRate=18.8237ns
BM_ArrayBoolOr/10000/50/20              176469 ns       176399 ns         3967 RowInvRate=17.6399ns
// Args: Number of rows / Null percentage / Array length
BM_ArrayCountVarchar/1000/0/20            7862 ns         7734 ns        90380 RowInvRate=7.73351ns
BM_ArrayCountVarchar/10000/0/20          54991 ns        54876 ns        12712 RowInvRate=5.48764ns
BM_ArrayCountVarchar/1000/10/20          11769 ns        11665 ns        59731 RowInvRate=11.6649ns
BM_ArrayCountVarchar/10000/10/20         93428 ns        93320 ns         7515 RowInvRate=9.33205ns
BM_ArrayCountVarchar/1000/50/20          11860 ns        11769 ns        59012 RowInvRate=11.7691ns
BM_ArrayCountVarchar/10000/50/20         93447 ns        93338 ns         7456 RowInvRate=9.33384ns
BM_ArrayCountVarchar/1000/0/100           7993 ns         7844 ns        89847 RowInvRate=7.84388ns
BM_ArrayCountVarchar/10000/0/100         93222 ns        92988 ns         7577 RowInvRate=9.29879ns
BM_ArrayCountVarchar/1000/10/100         29251 ns        29134 ns        23915 RowInvRate=29.1336ns
BM_ArrayCountVarchar/10000/10/100       298464 ns       298247 ns         2361 RowInvRate=29.8247ns
BM_ArrayCountVarchar/1000/50/100         29501 ns        29348 ns        23776 RowInvRate=29.3479ns
BM_ArrayCountVarchar/10000/50/100       271210 ns       271083 ns         2603 RowInvRate=27.1083ns
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
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BOOLEAN));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(probability);

    int total_rows = 0;
    for (auto _ : state) {
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
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT));
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
    for (auto _ : state) {
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
