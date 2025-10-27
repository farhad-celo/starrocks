#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "exprs/time_functions.h"
#include "runtime/types.h"
#include "testutil/assert.h"

/*
2025-07-22T15:03:17+00:00
Running ./be/build_Release/src/bench/celonis/output/to_days_bench
Run on (32 X 3245.45 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.15, 6.16, 3.99
// Number of rows
---------------------------------------------------------------------------
Benchmark                 Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------
BM_ToDays/4096         3616 ns         2352 ns       304701 RowInvRate=574.211ps
BM_ToDays/10000        5499 ns         5357 ns       126620 RowInvRate=535.729ps
BM_ToDays/100000      44586 ns        44441 ns        15299 RowInvRate=444.411ps
*/

namespace starrocks {

TimestampValue generate_random_timestamp() {
    auto seed = 42;
    std::mt19937_64 rng(seed);
    // Define distribution range (Unix timestamps in milliseconds)
    // This range covers from 1970 to ~2100
    std::uniform_int_distribution<int64_t> dist(0, 4102444800000);
    // Generate and return random timestamp
    int64_t millis = dist(rng);
    TimestampValue timestamp;
    timestamp.from_unix_second(millis / 1000, millis % 1000 * 1000);
    return timestamp;
}

static void BM_ToDays(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue.
    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(generate_random_timestamp());
        }
        ctx->set_constant_columns({nullptr});
        state.ResumeTiming();
        EXPECT_TRUE(TimeFunctions::to_days(ctx.get(), {input_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows
BENCHMARK(BM_ToDays)->ArgsProduct({{4096, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();