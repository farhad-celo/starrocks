#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/celonis/remap_timestamp_weekday.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "util/date_func.h"

/*
2025-07-13T14:55:00+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_timestamp_weekday_bench
Run on (32 X 3263.73 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.54, 3.48, 2.39
// Number of rows
------------------------------------------------------------------------------------------
Benchmark                                Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------------
BM_RemapTimestampWeekday/4096        29689 ns        29679 ns        23549 RowInvRate=7.24585ns
BM_RemapTimestampWeekday/10000       72005 ns        71990 ns         9751 RowInvRate=7.19905ns
BM_RemapTimestampWeekday/100000     705480 ns       705456 ns          992 RowInvRate=7.05456ns
*/

namespace starrocks {

TimestampValue generate_random_timestamp(std::mt19937_64& rng) {
    // Define distribution range (Unix timestamps in milliseconds)
    // This range covers from 1970 to ~2100
    std::uniform_int_distribution<int64_t> dist(0, 4102444800000);
    int64_t millis = dist(rng);
    TimestampValue timestamp;
    timestamp.from_unix_second(millis / 1000, millis % 1000 * 1000);
    return timestamp;
}

static void BM_RemapTimestampWeekday(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue
    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_DATETIME)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        auto seed = 42;
        std::mt19937_64 rng(seed);

        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(generate_random_timestamp(rng));
        }
        ctx->set_constant_columns({nullptr});
        state.ResumeTiming();
        EXPECT_TRUE(CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday(ctx.get(), {input_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows
BENCHMARK(BM_RemapTimestampWeekday)->ArgsProduct({{4096, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();
