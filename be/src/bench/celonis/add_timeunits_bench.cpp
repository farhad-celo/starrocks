#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-03-12T02:19:04+00:00
Running ./be/build_Release/src/bench/celonis/output/add_timeunits_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.51, 6.05, 5.67
// Args: Number of rows
------------------------------------------------------------------------------------
Benchmark                          Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------
BM_AddMilliseconds/10000    88469871 ns     88465196 ns            8 RowInvRate=8.84652us
BM_AddMilliseconds/100000  885410823 ns    885380984 ns            1 RowInvRate=8.85381us
BM_AddSeconds/10000           946053 ns       945991 ns          739 RowInvRate=94.5991ns
BM_AddSeconds/100000         9437872 ns      9437571 ns           74 RowInvRate=94.3757ns
BM_AddMinutes/10000           856399 ns       856300 ns          814 RowInvRate=85.63ns
BM_AddMinutes/100000         8510188 ns      8509635 ns           82 RowInvRate=85.0964ns
BM_AddHours/10000             780426 ns       780325 ns          901 RowInvRate=78.0325ns
BM_AddHours/100000           7769933 ns      7769339 ns           90 RowInvRate=77.6934ns
BM_AddDays/10000              719913 ns       719840 ns          972 RowInvRate=71.984ns
BM_AddDays/100000            7142611 ns      7141979 ns           98 RowInvRate=71.4198ns
BM_AddWorkdays/10000          690304 ns       690212 ns         1016 RowInvRate=69.0212ns
BM_AddWorkdays/100000        6854077 ns      6853494 ns          102 RowInvRate=68.5349ns
*/

static const phmap::flat_hash_map<std::string, int64_t> TIME_UNIT_TO_MS = {
        {"DAYS",         86400000L},
        {"WORKDAYS",     86400000L},
        {"HOURS",        3600000L},
        {"MINUTES",      60000L},
        {"SECONDS",      1000L},
        {"MILLISECONDS", 1L}
};

static void do_bench(benchmark::State& state, const std::string& time_unit) {
    date::init_date_cache(); // This is needed for using TimestampValue.
    int num_rows = state.range(0);

    int64_t factor = TIME_UNIT_TO_MS.find(time_unit)->second;
    using UniformInt = std::uniform_int_distribution<int64_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_timestamp_increase(1, 1000LL * 3600 * 24 * 365 * 40); // 40 years

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    TimestampValue timestamp;
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto timestamp_column =
                ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto add_column =
            ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        auto unit_column =
                ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto calendar_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto calendar_id_column = ColumnHelper::create_const_null_column(num_rows);
        calendar_column->append_datum(DatumArray{});
        calendar_column = ConstColumn::create(calendar_column, num_rows);
        unit_column->append_datum(Slice(time_unit));
        unit_column = ConstColumn::create(unit_column, num_rows);

        for (int i = 0; i < num_rows; i++) {
            int64_t unix_millis = uniform_timestamp_increase(rng);
            timestamp.from_unix_second(unix_millis / 1000);
            timestamp_column->append_datum(timestamp);
            int64_t add_millis = uniform_timestamp_increase(rng);
            add_column->append_datum(static_cast<int64_t>(add_millis) / factor);
        }
        ctx->set_constant_columns({nullptr, nullptr, unit_column, calendar_column, nullptr});
        state.ResumeTiming();
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_prepare(ctx.get(),
                                                                       FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::add_timeunits_calendar_prepare(ctx.get(),
                                                                     FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::add_timeunits_calendar(ctx.get(),
                                                                 {timestamp_column, add_column, unit_column,
                                                                  calendar_column,
                                                                  calendar_id_column}).ok());
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_close(ctx.get(),
                                                                     FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::add_timeunits_calendar_close(ctx.get(),
                                                                   FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_AddMilliseconds(benchmark::State& state) {
    do_bench(state, "MILLISECONDS");
}

static void BM_AddSeconds(benchmark::State& state) {
    do_bench(state, "SECONDS");
}

static void BM_AddMinutes(benchmark::State& state) {
    do_bench(state, "MINUTES");
}

static void BM_AddHours(benchmark::State& state) {
    do_bench(state, "HOURS");
}

static void BM_AddDays(benchmark::State& state) {
    do_bench(state, "DAYS");
}

static void BM_AddWorkdays(benchmark::State& state) {
    do_bench(state, "WORKDAYS");
}

// Args: Number of rows
BENCHMARK(BM_AddMilliseconds)->ArgsProduct({{10000, 100000}});
BENCHMARK(BM_AddSeconds)->ArgsProduct({{10000, 100000}});
BENCHMARK(BM_AddMinutes)->ArgsProduct({{10000, 100000}});
BENCHMARK(BM_AddHours)->ArgsProduct({{10000, 100000}});
BENCHMARK(BM_AddDays)->ArgsProduct({{10000, 100000}});
BENCHMARK(BM_AddWorkdays)->ArgsProduct({{10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();
