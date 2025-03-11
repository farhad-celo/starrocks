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
2025-03-11T01:31:50+00:00
Running ./be/build_Release/src/bench/celonis/output/upper_lower_bench
Run on (32 X 3081.57 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.82, 7.52, 4.45
// Args: Number of rows / Average string length
-----------------------------------------------------------------------------
Benchmark                   Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------
BM_Upper/10000/10      370655 ns       370494 ns         1885 RowInvRate=37.0494ns
BM_Upper/100000/10    3680285 ns      3679957 ns          191 RowInvRate=36.7996ns
BM_Upper/10000/20      730376 ns       730203 ns          950 RowInvRate=73.0203ns
BM_Upper/100000/20    7204934 ns      7204448 ns           97 RowInvRate=72.0445ns
BM_Upper/10000/40     1453130 ns      1452885 ns          482 RowInvRate=145.288ns
BM_Upper/100000/40   14380698 ns     14379439 ns           49 RowInvRate=143.794ns
BM_Lower/10000/10      412091 ns       411648 ns         1742 RowInvRate=41.1648ns
BM_Lower/100000/10    3954668 ns      3954045 ns          176 RowInvRate=39.5404ns
BM_Lower/10000/20      793930 ns       793698 ns          880 RowInvRate=79.3698ns
BM_Lower/100000/20    7774539 ns      7773989 ns           90 RowInvRate=77.7399ns
BM_Lower/10000/40     1573628 ns      1573410 ns          444 RowInvRate=157.341ns
BM_Lower/100000/40   15422159 ns     15420519 ns           45 RowInvRate=154.205ns
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
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
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
