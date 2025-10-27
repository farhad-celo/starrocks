#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "util/date_func.h"

/*
2025-07-13T10:48:22+00:00
Running ./be/build_Release/src/bench/celonis/output/date_match_bench
Run on (32 X 3244.22 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.28, 10.53, 9.19
// Number of rows
------------------------------------------------------------------------------
Benchmark                    Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------
BM_DateMatch/1000        15970 ns        15929 ns        44194 RowInvRate=15.9295ns
BM_DateMatch/10000      150416 ns       150356 ns         4596 RowInvRate=15.0356ns
BM_DateMatch/100000    1416034 ns      1415767 ns          450 RowInvRate=14.1577ns
*/

namespace starrocks {

TypeDescriptor TYPE_ARRAY_BIGINT = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT));

TimestampValue generate_random_timestamp(std::mt19937_64& rng) {
    // Define distribution range (Unix timestamps in milliseconds)
    // This range covers from 1970 to ~2100
    std::uniform_int_distribution<int64_t> dist(0, 4102444800000);
    int64_t millis = dist(rng);
    TimestampValue timestamp;
    timestamp.from_unix_second(millis / 1000, millis % 1000 * 1000);
    return timestamp;
}

ColumnPtr generate_filter_array(std::mt19937_64& rng, int num_rows, int64_t min_val, int64_t max_val) {
    ColumnPtr array_column = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BIGINT), true);
    std::uniform_int_distribution<int> array_size_dist(1, 5); // 1-5 elements per array
    std::uniform_int_distribution<int64_t> value_dist(min_val, max_val);

    DatumArray array_datum;
    int array_size = array_size_dist(rng);
    for (int j = 0; j < array_size; j++) {
        array_datum.push_back(Datum(value_dist(rng)));
    }
    array_column->append_datum(Datum(array_datum));
    array_column = ConstColumn::create(array_column, num_rows);
    return array_column;
}

static void BM_DateMatch(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue
    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        auto seed = 42;
        std::mt19937_64 rng(seed);

        // Generate timestamp column
        ColumnPtr timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        for (int i = 0; i < num_rows; i++) {
            timestamp_column->append_datum(generate_random_timestamp(rng));
        }

        // Generate filter arrays for years, quarters, months, weeks, days
        ColumnPtr years_column = generate_filter_array(rng, num_rows, 1970, 2100); // years
        ColumnPtr quarters_column = generate_filter_array(rng, num_rows, 1, 4);    // quarters (1-4)
        ColumnPtr months_column = generate_filter_array(rng, num_rows, 1, 12);     // months (1-12)
        ColumnPtr weeks_column = generate_filter_array(rng, num_rows, 1, 53);      // weeks (1-53)
        ColumnPtr days_column = generate_filter_array(rng, num_rows, 1, 31);       // days (1-31)

        ctx->set_constant_columns({nullptr, years_column, quarters_column, months_column, weeks_column, days_column});
        // Prepare function context
        EXPECT_TRUE(CelonisTimeFunctions::date_match_prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        EXPECT_TRUE(CelonisTimeFunctions::date_match_prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());

        state.ResumeTiming();
        auto result = CelonisTimeFunctions::date_match(
                ctx.get(), {timestamp_column, years_column, quarters_column, months_column, weeks_column, days_column});
        EXPECT_TRUE(result.ok());
        EXPECT_FALSE(result.value()->only_null());
        state.PauseTiming();

        // Clean up function context
        EXPECT_TRUE(CelonisTimeFunctions::date_match_close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        EXPECT_TRUE(CelonisTimeFunctions::date_match_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        state.ResumeTiming();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows
BENCHMARK(BM_DateMatch)->ArgsProduct({{1000, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();