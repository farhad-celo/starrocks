#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.cpp"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-07-29T22:50:04+00:00
Running ./be/build_Release/src/bench/celonis/output/upper_lower_bench
Run on (32 X 3289.17 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.44, 4.70, 2.99
// Args: Number of rows / Average string length
-----------------------------------------------------------------------------
Benchmark                   Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------
BM_Upper/10000/10      367237 ns       366652 ns         1904 RowInvRate=36.6652ns
BM_Upper/100000/10    3555056 ns      3554382 ns          198 RowInvRate=35.5438ns
BM_Upper/10000/20      707189 ns       706971 ns          988 RowInvRate=70.6971ns
BM_Upper/100000/20    7009578 ns      7008924 ns           99 RowInvRate=70.0892ns
BM_Upper/10000/40     1411637 ns      1411394 ns          496 RowInvRate=141.139ns
BM_Upper/100000/40   13980038 ns     13979172 ns           50 RowInvRate=139.792ns
BM_Lower/10000/10      360179 ns       359970 ns         1941 RowInvRate=35.997ns
BM_Lower/100000/10    3596964 ns      3596285 ns          196 RowInvRate=35.9628ns
BM_Lower/10000/20      711125 ns       710901 ns          974 RowInvRate=71.0901ns
BM_Lower/100000/20    7037106 ns      7036473 ns          100 RowInvRate=70.3647ns
BM_Lower/10000/40     1408930 ns      1408748 ns          499 RowInvRate=140.875ns
BM_Lower/100000/40   13971299 ns     13970168 ns           50 RowInvRate=139.702ns
*/

std::string generate_random_string(int min_len, int max_len) {
    std::string chars =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937 gen(rd());

    // Generate a random length within the specified range
    std::uniform_int_distribution<> length_distrib(min_len, max_len);
    int len = length_distrib(gen);

    std::uniform_int_distribution<> char_distrib(0, chars.size() - 1);

    std::string random_string;
    for (int i = 0; i < len; ++i) {
        random_string += chars[char_distrib(gen)];
    }
    return random_string;
}

using ScalarFunction = StatusOr<ColumnPtr> (*)(FunctionContext* context, const Columns& columns);

static void bench(benchmark::State& state, ScalarFunction scalar_function) {
    int num_rows = state.range(0);
    int avg_length = state.range(1);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int min_length = avg_length - int(avg_length * 2 / 10);
    int max_length = avg_length + int(avg_length * 2 / 10);
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(Slice(generate_random_string(min_length, max_length)));
        }
        state.ResumeTiming();
        auto result = scalar_function(ctx.get(), {input_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_Upper(benchmark::State& state) {
    bench(state, CelonisStringFunctions::upper);
}

static void BM_Lower(benchmark::State& state) {
    bench(state, CelonisStringFunctions::lower);
}

// Args: Number of rows / Average string length
BENCHMARK(BM_Upper)->ArgsProduct({{10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_Lower)->ArgsProduct({{10000, 100000}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
