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
2025-03-11T18:46:43+00:00
Running ./be/build_Release/src/bench/celonis/output/array_functions_bench
Run on (32 X 2998.09 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.35, 0.62, 1.30
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
// Args: Number of rows / Number of unique elements in the array / Max number of duplicates per unique element
BM_DedupSortedByVARCHAR/10000/10/5     2420441 ns      2420222 ns          295 RowInvRate=242.022ns
BM_DedupSortedByVARCHAR/10000/20/5     6253723 ns      6253078 ns          112 RowInvRate=625.308ns
BM_DedupSortedByVARCHAR/10000/10/10    5730804 ns      5730227 ns          163 RowInvRate=573.023ns
BM_DedupSortedByVARCHAR/10000/20/10    9325136 ns      9324398 ns           75 RowInvRate=932.44ns
// Args: Number of rows / True percentage / Array length
BM_ArrayBoolOr/1000/1/10                  9381 ns         9333 ns        74790 RowInvRate=9.33322ns
BM_ArrayBoolOr/10000/1/10                82625 ns        82539 ns         8425 RowInvRate=8.25394ns
BM_ArrayBoolOr/1000/10/10                 9382 ns         9334 ns        75406 RowInvRate=9.33426ns
BM_ArrayBoolOr/10000/10/10               82656 ns        82570 ns         8478 RowInvRate=8.257ns
BM_ArrayBoolOr/1000/50/10                 9376 ns         9332 ns        74876 RowInvRate=9.33185ns
BM_ArrayBoolOr/10000/50/10               82567 ns        82495 ns         8511 RowInvRate=8.24948ns
BM_ArrayBoolOr/1000/1/20                  9388 ns         9347 ns        74311 RowInvRate=9.34745ns
BM_ArrayBoolOr/10000/1/20                82787 ns        82727 ns         8505 RowInvRate=8.27271ns
BM_ArrayBoolOr/1000/10/20                 9521 ns         9473 ns        74098 RowInvRate=9.47344ns
BM_ArrayBoolOr/10000/10/20               82814 ns        82744 ns         8475 RowInvRate=8.27439ns
BM_ArrayBoolOr/1000/50/20                 9396 ns         9351 ns        74846 RowInvRate=9.35054ns
BM_ArrayBoolOr/10000/50/20               82867 ns        82795 ns         8461 RowInvRate=8.27952ns
// Args: Number of rows / Null percentage / Array length
BM_ArrayCountVarchar/1000/0/20            7788 ns         7678 ns        91441 RowInvRate=7.67766ns
BM_ArrayCountVarchar/10000/0/20          55052 ns        54935 ns        12709 RowInvRate=5.49348ns
BM_ArrayCountVarchar/1000/10/20          11394 ns        11343 ns        61868 RowInvRate=11.3434ns
BM_ArrayCountVarchar/10000/10/20         95358 ns        95275 ns         7344 RowInvRate=9.52754ns
BM_ArrayCountVarchar/1000/50/20          11262 ns        11225 ns        62460 RowInvRate=11.2255ns
BM_ArrayCountVarchar/10000/50/20         95092 ns        95030 ns         7346 RowInvRate=9.503ns
BM_ArrayCountVarchar/1000/0/100           8025 ns         7901 ns        89090 RowInvRate=7.90054ns
BM_ArrayCountVarchar/10000/0/100         93421 ns        93283 ns         7501 RowInvRate=9.32834ns
BM_ArrayCountVarchar/1000/10/100         28904 ns        28810 ns        24276 RowInvRate=28.8104ns
BM_ArrayCountVarchar/10000/10/100       269685 ns       269603 ns         2600 RowInvRate=26.9603ns
BM_ArrayCountVarchar/1000/50/100         29059 ns        28944 ns        24207 RowInvRate=28.9442ns
BM_ArrayCountVarchar/10000/50/100       270219 ns       270108 ns         2583 RowInvRate=27.0108ns
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

        ASSERT_TRUE(result.ok()) << result.status().message();
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
