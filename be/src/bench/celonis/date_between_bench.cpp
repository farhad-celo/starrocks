#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

/*
2025-07-12T22:31:49+00:00
Running ./be/build_Release/src/bench/celonis/output/date_between_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.98, 2.93, 2.54
// Number of rows
--------------------------------------------------------------------------------
Benchmark                      Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------
BM_DateBetween/1000         9889 ns         9880 ns        70788 RowInvRate=9.87989ns
BM_DateBetween/10000      128784 ns       128695 ns         5476 RowInvRate=12.8695ns
BM_DateBetween/100000    1365829 ns      1365450 ns          511 RowInvRate=13.6545ns
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

TimestampValue generate_range_timestamp(const TimestampValue& base, std::mt19937_64& rng, bool is_end = false) {
    // Generate timestamp within reasonable range of base timestamp
    std::uniform_int_distribution<int64_t> offset_dist(-86400000, 86400000); // +/- 1 day in milliseconds
    int64_t base_millis = base.to_unix_second() * 1000;
    int64_t offset = offset_dist(rng);
    if (is_end) {
        offset = std::abs(offset); // Ensure end is after begin for some test cases
    }
    int64_t new_millis = base_millis + offset;
    TimestampValue timestamp;
    timestamp.from_unix_second(new_millis / 1000, (new_millis % 1000) * 1000);
    return timestamp;
}

static void BM_DateBetween(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue
    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_DATETIME),
                                                        TypeDescriptor::from_logical_type(TYPE_DATETIME),
                                                        TypeDescriptor::from_logical_type(TYPE_DATETIME)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        auto seed = 42;
        std::mt19937_64 rng(seed);

        ColumnPtr first_date_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        ColumnPtr second_date_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        ColumnPtr third_date_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);

        for (int i = 0; i < num_rows; i++) {
            TimestampValue first_date = generate_random_timestamp(rng);
            TimestampValue second_date = generate_range_timestamp(first_date, rng);
            TimestampValue third_date = generate_range_timestamp(second_date, rng, true);

            first_date_column->append_datum(first_date);
            second_date_column->append_datum(second_date);
            third_date_column->append_datum(third_date);
        }
        ctx->set_constant_columns({nullptr, nullptr, nullptr});
        state.ResumeTiming();
        EXPECT_TRUE(CelonisTimeFunctions::date_between(ctx.get(),
                                                       {first_date_column, second_date_column, third_date_column})
                            .ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows
BENCHMARK(BM_DateBetween)->ArgsProduct({{1000, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();