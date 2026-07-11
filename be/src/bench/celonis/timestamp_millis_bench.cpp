#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-03-10T20:38:12+00:00
Running ./be/build_Release/src/bench/celonis/output/timestamp_millis_bench
Run on (32 X 3244.02 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 10.90, 7.22, 4.75
// Number of rows
------------------------------------------------------------------------------------
Benchmark                          Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------
BM_TimestampMillis/1000         7846 ns         7768 ns        92080 RowInvRate=7.76807ns
BM_TimestampMillis/10000       63643 ns        63601 ns        10912 RowInvRate=6.36012ns
BM_TimestampMillis/100000     784427 ns       777798 ns         1132 RowInvRate=7.77798ns
*/

int64_t generate_random_unix_milliseconds() {
    auto seed = 42;
    std::mt19937_64 rng(seed);
    // Define distribution range (Unix timestamps in milliseconds)
    // This range covers from 1970 to ~2100
    std::uniform_int_distribution<int64_t> dist(0, 4102444800000);
    // Generate and return random timestamp
    return dist(rng);
}

static void BM_TimestampMillis(benchmark::State& state) {
    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_BIGINT)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_DATETIME);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(generate_random_unix_milliseconds());
        }
        ctx->set_constant_columns({nullptr});
        state.ResumeTiming();
        EXPECT_TRUE(CelonisTimeFunctions::timestamp_millis(ctx.get(), {input_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows
BENCHMARK(BM_TimestampMillis)->ArgsProduct({{1000, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();