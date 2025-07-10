#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <functional>
#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "modules/query/calendars.pb.h"
#include "runtime/types.h"
#include "testutil/assert.h"

#include "calendar_util.h"

/*
2025-07-09T14:58:43+00:00
Running ./be/build_Release/src/bench/celonis/output/timeunits_between_calendar_bench
Run on (32 X 3230.72 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.25, 5.37, 4.90
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id (only for factory calendar)
-----------------------------------------------------------------------------------------------------------
Benchmark                                                 Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------
BM_TimeunitsBetweenFactoryCalendar/1000/2/10          84872 ns        84918 ns         8037 RowInvRate=84.9182ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/10        735732 ns       735736 ns          956 RowInvRate=73.5736ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/10      7161025 ns      7161101 ns           98 RowInvRate=71.611ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/10          94901 ns        94935 ns         7396 RowInvRate=94.935ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/10        749854 ns       749847 ns          925 RowInvRate=74.9847ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/10      7135363 ns      7135447 ns           97 RowInvRate=71.3545ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/10         108160 ns       108210 ns         6518 RowInvRate=108.21ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/10        757333 ns       757361 ns          928 RowInvRate=75.7361ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/10      7193238 ns      7193291 ns           98 RowInvRate=71.9329ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/100        135348 ns       135426 ns         5180 RowInvRate=135.426ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/100       788639 ns       788619 ns          883 RowInvRate=78.8619ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/100     7233796 ns      7233783 ns           97 RowInvRate=72.3378ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/100        199873 ns       199975 ns         3555 RowInvRate=199.975ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/100       848427 ns       848401 ns          814 RowInvRate=84.8401ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/100     7281820 ns      7281942 ns           97 RowInvRate=72.8194ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/100        327239 ns       327361 ns         2135 RowInvRate=327.361ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/100      1019544 ns      1019577 ns          709 RowInvRate=101.958ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/100     7456304 ns      7456346 ns           90 RowInvRate=74.5635ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/1000       702725 ns       702851 ns          979 RowInvRate=702.851ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/1000     1376155 ns      1376208 ns          509 RowInvRate=137.621ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/1000    7828095 ns      7828028 ns           88 RowInvRate=78.2803ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/1000      1395142 ns      1395266 ns          505 RowInvRate=1.39527us
BM_TimeunitsBetweenFactoryCalendar/10000/4/1000     2052207 ns      2052177 ns          341 RowInvRate=205.218ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/1000    8563631 ns      8563630 ns           83 RowInvRate=85.6363ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/1000      2740841 ns      2741013 ns          256 RowInvRate=2.74101us
BM_TimeunitsBetweenFactoryCalendar/10000/8/1000     3409782 ns      3409800 ns          206 RowInvRate=340.98ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/1000   10211901 ns     10211685 ns           70 RowInvRate=102.117ns
BM_TimeunitsBetweenWeekdayCalendar/1000/2            244477 ns       244512 ns         2870 RowInvRate=244.512ns
BM_TimeunitsBetweenWeekdayCalendar/10000/2          2352982 ns      2352934 ns          298 RowInvRate=235.293ns
BM_TimeunitsBetweenWeekdayCalendar/100000/2        23353575 ns     23353174 ns           30 RowInvRate=233.532ns
BM_TimeunitsBetweenWeekdayCalendar/1000/4            241280 ns       241297 ns         2891 RowInvRate=241.297ns
BM_TimeunitsBetweenWeekdayCalendar/10000/4          2297700 ns      2297614 ns          305 RowInvRate=229.761ns
BM_TimeunitsBetweenWeekdayCalendar/100000/4        23086094 ns     23085837 ns           31 RowInvRate=230.858ns
BM_TimeunitsBetweenWeekdayCalendar/1000/8            276573 ns       276645 ns         2762 RowInvRate=276.645ns
BM_TimeunitsBetweenWeekdayCalendar/10000/8          2340128 ns      2340104 ns          299 RowInvRate=234.01ns
BM_TimeunitsBetweenWeekdayCalendar/100000/8        27869634 ns     26239545 ns           30 RowInvRate=262.395ns
*/

namespace starrocks {

namespace {

using UniformInt = std::uniform_int_distribution<int32_t>;

void run_benchmark(benchmark::State& state, const CreateCalendar& create_calendar) {
    date::init_date_cache(); // This is needed for using TimestampValue.
    int num_rows = state.range(0);
    int num_calendar_ids = state.range(1);

    std::vector<std::string> calendar_ids;
    calendar_ids.reserve(num_calendar_ids);
    for (auto i = 0; i < num_calendar_ids; i++) {
        calendar_ids.push_back("calendar_" + std::to_string(i));
    }

    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_calendar_id(0, num_calendar_ids - 1);
    UniformInt uniform_timestamp_increase(1, 3600 * 24 * 365 * 10); // 10 years

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    celonis::accelerator::Calendar calendar_proto = create_calendar(calendar_ids, rng);
    std::optional<std::string> serialized_calendar = to_base64_encoded_string(calendar_proto, 1LL << 30, true);
    ASSERT_TRUE(serialized_calendar.has_value());
    DatumArray calendar_array;
    calendar_array.emplace_back(Slice(serialized_calendar.value()));

    TimestampValue from_timestamp, to_timestamp;
    int from_unix_seconds, to_unix_seconds;
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto from_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto to_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto unit_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto calendar_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto calendar_id_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        unit_column->append_datum("SECONDS");
        unit_column = ConstColumn::create(unit_column, num_rows);

        calendar_column->append_datum(calendar_array);
        calendar_column = ConstColumn::create(calendar_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            from_unix_seconds = uniform_timestamp_increase(rng);
            to_unix_seconds = uniform_timestamp_increase(rng);
            if (from_unix_seconds > to_unix_seconds) {
                std::swap(from_unix_seconds, to_unix_seconds);
            }
            from_timestamp.from_unix_second(from_unix_seconds);
            from_timestamp_column->append_datum(from_timestamp);
            to_timestamp.from_unix_second(to_unix_seconds);
            to_timestamp_column->append_datum(to_timestamp);
            calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
        }
        ctx->set_constant_columns({nullptr, nullptr, unit_column, calendar_column, nullptr});
        state.ResumeTiming();
        ASSERT_OK(CelonisTimeFunctions::timeunits_between_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::timeunits_between_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::timeunits_between_calendar(
                            ctx.get(), {from_timestamp_column, to_timestamp_column, unit_column, calendar_column,
                                        calendar_id_column})
                            .ok());
        ASSERT_OK(CelonisTimeFunctions::timeunits_between_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::timeunits_between_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

} // namespace

static void BM_TimeunitsBetweenFactoryCalendar(benchmark::State& state) {
    const int num_calendar_entries = state.range(2);

    run_benchmark(state, [&](const auto& calendar_ids, auto& rng) {
        return create_factory_calendar(calendar_ids, rng, num_calendar_entries);
    });
}

static void BM_TimeunitsBetweenWeekdayCalendar(benchmark::State& state) {
    run_benchmark(state, create_weekday_calendar);
}

BENCHMARK(BM_TimeunitsBetweenFactoryCalendar)
        ->ArgsProduct({
                {1000, 10000, 100000}, // Number of rows
                {2, 4, 8}, // Number of calendar IDs
                {10, 100, 1000}, // Number of calendar entries per ID
        });

BENCHMARK(BM_TimeunitsBetweenWeekdayCalendar)
        ->ArgsProduct({
                {1000, 10000, 100000}, // Number of rows
                {2, 4, 8}, // Number of calendar IDs
        });

} // namespace starrocks

BENCHMARK_MAIN();
