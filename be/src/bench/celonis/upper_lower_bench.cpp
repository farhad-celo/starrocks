#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-11-25T10:46:21+00:00
Running ./be/build_Release/src/bench/celonis/output/upper_lower_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.33, 5.95, 4.15
// Args: Number of rows / Average string length
-----------------------------------------------------------------------------
Benchmark                   Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------
BM_Upper/10000/10       56882 ns        56799 ns        12308 RowInvRate=5.67989ns
BM_Upper/100000/10     561020 ns       560804 ns         1256 RowInvRate=5.60804ns
BM_Upper/10000/20      103988 ns       103920 ns         6640 RowInvRate=10.392ns
BM_Upper/100000/20    1054494 ns      1054115 ns          675 RowInvRate=10.5412ns
BM_Upper/10000/40      203007 ns       202962 ns         3442 RowInvRate=20.2962ns
BM_Upper/100000/40    1908906 ns      1908417 ns          371 RowInvRate=19.0842ns
BM_Lower/10000/10       58031 ns        57891 ns        12117 RowInvRate=5.78913ns
BM_Lower/100000/10     577893 ns       577551 ns         1240 RowInvRate=5.77551ns
BM_Lower/10000/20      107800 ns       107683 ns         6544 RowInvRate=10.7683ns
BM_Lower/100000/20    1038114 ns      1037764 ns          668 RowInvRate=10.3776ns
BM_Lower/10000/40      204665 ns       204601 ns         3424 RowInvRate=20.4601ns
BM_Lower/100000/40    1892191 ns      1891690 ns          379 RowInvRate=18.9169ns
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
