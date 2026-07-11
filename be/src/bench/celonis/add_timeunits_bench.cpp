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
2025-09-15T17:33:11+00:00
Running ./be/build_Release/src/bench/celonis/output/add_timeunits_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.69, 1.84, 0.73
// Args: Number of rows
------------------------------------------------------------------------------------
Benchmark                          Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------
BM_AddMilliseconds/10000      100574 ns       100507 ns         7000 RowInvRate=10.0507ns
BM_AddMilliseconds/100000     970069 ns       970054 ns          722 RowInvRate=9.70054ns
BM_AddSeconds/10000           100226 ns       100153 ns         6998 RowInvRate=10.0153ns
BM_AddSeconds/100000          983419 ns       983407 ns          720 RowInvRate=9.83407ns
BM_AddMinutes/10000           100629 ns       100585 ns         6957 RowInvRate=10.0585ns
BM_AddMinutes/100000         1055432 ns      1055260 ns          710 RowInvRate=10.5526ns
BM_AddHours/10000             106511 ns       106443 ns         6249 RowInvRate=10.6443ns
BM_AddHours/100000           1043993 ns      1043859 ns          717 RowInvRate=10.4386ns
BM_AddDays/10000              101157 ns       101094 ns         6915 RowInvRate=10.1094ns
BM_AddDays/100000             981283 ns       981315 ns          717 RowInvRate=9.81315ns
BM_AddWorkdays/10000          100562 ns       100516 ns         6879 RowInvRate=10.0516ns
BM_AddWorkdays/100000         976092 ns       976101 ns          710 RowInvRate=9.76101ns
*/

static const phmap::flat_hash_map<std::string, int64_t> TIME_UNIT_TO_MS = {{"DAYS", 86400000L}, {"WORKDAYS", 86400000L},
                                                                           {"HOURS", 3600000L}, {"MINUTES", 60000L},
                                                                           {"SECONDS", 1000L},  {"MILLISECONDS", 1L}};

static void do_bench(benchmark::State& state, const std::string& time_unit) {
    date::init_date_cache(); // This is needed for using TimestampValue.
    int num_rows = state.range(0);

    int64_t factor = TIME_UNIT_TO_MS.find(time_unit)->second;
    using UniformInt = std::uniform_int_distribution<int64_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_timestamp_increase(1, 1000LL * 3600 * 24 * 365 * 40); // 40 years

    std::vector<FunctionContext::TypeDesc> arg_types = {
            TypeDescriptor(TYPE_DATETIME), TypeDescriptor(TYPE_BIGINT), TypeDescriptor(TYPE_VARCHAR),
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), TypeDescriptor(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor(TYPE_DATETIME);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    TimestampValue timestamp;
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto add_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        auto unit_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
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
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::add_timeunits_calendar(
                            ctx.get(), {timestamp_column, add_column, unit_column, calendar_column, calendar_id_column})
                            .ok());
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
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
