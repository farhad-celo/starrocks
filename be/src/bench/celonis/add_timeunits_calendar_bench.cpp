#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "calendar_util.h"
#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "modules/query/calendars.pb.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-06-01T22:00:31+00:00
Running ./be/build_Release/src/bench/celonis/output/add_timeunits_calendar_bench
Run on (32 X 3232.2 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 12.30, 6.89, 4.19
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id
---------------------------------------------------------------------------------------------------------------
Benchmark                                                     Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------------------
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/10         107784 ns       107801 ns         6683 RowInvRate=107.801ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/10        947640 ns       947611 ns          739 RowInvRate=94.7611ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/10      9465985 ns      9466126 ns           75 RowInvRate=94.6613ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/10         108367 ns       108399 ns         6497 RowInvRate=108.399ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/10        913491 ns       913500 ns          769 RowInvRate=91.35ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/10      8988262 ns      8988194 ns           78 RowInvRate=89.8819ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/10         127043 ns       127086 ns         5498 RowInvRate=127.086ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/10        937149 ns       937140 ns          749 RowInvRate=93.714ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/10      9080439 ns      9080604 ns           76 RowInvRate=90.806ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/100        154607 ns       154654 ns         4457 RowInvRate=154.654ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/100       991118 ns       991140 ns          702 RowInvRate=99.114ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/100     9172688 ns      9172529 ns           76 RowInvRate=91.7253ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/100        209417 ns       209478 ns         3331 RowInvRate=209.478ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/100      1011119 ns      1011150 ns          687 RowInvRate=101.115ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/100     8928828 ns      8928597 ns           78 RowInvRate=89.286ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/100        331414 ns       331497 ns         2075 RowInvRate=331.497ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/100      1141040 ns      1141064 ns          615 RowInvRate=114.106ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/100     9129465 ns      9129589 ns           77 RowInvRate=91.2959ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/1000       679416 ns       679544 ns         1043 RowInvRate=679.544ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/1000     1563229 ns      1563224 ns          462 RowInvRate=156.322ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/1000    9975643 ns      9975561 ns           72 RowInvRate=99.7556ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/1000      1328035 ns      1327916 ns          531 RowInvRate=1.32792us
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/1000     2190593 ns      2190662 ns          331 RowInvRate=219.066ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/1000   10526922 ns     10526334 ns           66 RowInvRate=105.263ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/1000      2744866 ns      2744891 ns          257 RowInvRate=2.74489us
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/1000     3362007 ns      3361913 ns          203 RowInvRate=336.191ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/1000   11784465 ns     11784381 ns           61 RowInvRate=117.844ns
BM_AddTimeunitsCalendarWeekdayCalendar/1000/2          29951565 ns     29949630 ns           23 RowInvRate=29.9496us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/2        312696607 ns    312666988 ns            2 RowInvRate=31.2667us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/2      3059288097 ns   3058669850 ns            1 RowInvRate=30.5867us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/4          29256814 ns     29256570 ns           24 RowInvRate=29.2566us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/4        303877153 ns    303874208 ns            2 RowInvRate=30.3874us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/4      2976006763 ns   2975967762 ns            1 RowInvRate=29.7597us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/8          30141795 ns     30142030 ns           23 RowInvRate=30.142us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/8        295971590 ns    295960726 ns            2 RowInvRate=29.5961us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/8      2971714130 ns   2971670839 ns            1 RowInvRate=29.7167us
*/

static void run_benchmark(benchmark::State& state, const CreateCalendar& create_calendar) {
    date::init_date_cache(); // This is needed for using TimestampValue.
    int num_rows = state.range(0);
    int num_calendar_ids = state.range(1);

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_timestamp_increase(1, 3600 * 24 * 365 * 10); // 10 years
    UniformInt uniform_calendar_id(0, num_calendar_ids - 1);

    std::vector<std::string> calendar_ids;
    calendar_ids.reserve(num_calendar_ids);
    for (auto i = 0; i < num_calendar_ids; i++) {
        calendar_ids.push_back("calendar_" + std::to_string(i));
    }

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    celonis::accelerator::Calendar calendar_proto = create_calendar(calendar_ids, rng);
    std::optional<std::string> serialized_calendar = to_base64_encoded_string(calendar_proto);
    ASSERT_TRUE(serialized_calendar.has_value());
    DatumArray calendar_array;
    calendar_array.emplace_back(Slice(serialized_calendar.value()));

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
        auto calendar_id_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        unit_column->append_datum("SECONDS");
        unit_column = ConstColumn::create(unit_column, num_rows);

        calendar_column->append_datum(calendar_array);
        calendar_column = ConstColumn::create(calendar_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            int unix_seconds = uniform_timestamp_increase(rng);
            timestamp.from_unix_second(unix_seconds);
            timestamp_column->append_datum(timestamp);
            int add = uniform_timestamp_increase(rng);
            add_column->append_datum(static_cast<int64_t>(add));
            calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
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
static void BM_AddTimeunitsCalendarFactoryCalendar(benchmark::State& state) {
    int num_calendar_entries = state.range(2);

    run_benchmark(state, [&](const auto& calendar_ids, auto& rng) {
        return create_factory_calendar(calendar_ids, rng, num_calendar_entries);
    });
}

static void BM_AddTimeunitsCalendarWeekdayCalendar(benchmark::State& state) {
    run_benchmark(state, create_weekday_calendar);
}

BENCHMARK(BM_AddTimeunitsCalendarFactoryCalendar)
        ->ArgsProduct({
                {1000, 10000, 100000}, // number of rows
                {2, 4, 8},             // number of calendar ids
                {10, 100, 1000},       // number of calendar entries per id
        });

BENCHMARK(BM_AddTimeunitsCalendarWeekdayCalendar)
        ->ArgsProduct({
                {1000, 10000, 100000}, // number of rows
                {2, 4, 8},             // number of calendar ids
        });

} // namespace starrocks

BENCHMARK_MAIN();
