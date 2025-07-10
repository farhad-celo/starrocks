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
2025-07-09T14:56:43+00:00
Running ./be/build_Release/src/bench/celonis/output/add_timeunits_calendar_bench
Run on (32 X 3247.29 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.47, 5.64, 4.94
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id
---------------------------------------------------------------------------------------------------------------
Benchmark                                                     Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------------------
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/10         102865 ns       102907 ns         6757 RowInvRate=102.907ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/10        924506 ns       924522 ns          760 RowInvRate=92.4522ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/10      9013488 ns      9013183 ns           78 RowInvRate=90.1318ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/10         112999 ns       113036 ns         6148 RowInvRate=113.036ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/10        934172 ns       934179 ns          766 RowInvRate=93.4179ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/10      9048821 ns      9048694 ns           77 RowInvRate=90.4869ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/10         125167 ns       125219 ns         5578 RowInvRate=125.219ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/10        924985 ns       924988 ns          756 RowInvRate=92.4988ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/10      8876969 ns      8877060 ns           79 RowInvRate=88.7706ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/100        152460 ns       152518 ns         4565 RowInvRate=152.518ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/100       954370 ns       954381 ns          730 RowInvRate=95.4381ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/100     8936644 ns      8936742 ns           77 RowInvRate=89.3674ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/100        214157 ns       214249 ns         3266 RowInvRate=214.249ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/100      1022516 ns      1022541 ns          688 RowInvRate=102.254ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/100     9145488 ns      9145449 ns           77 RowInvRate=91.4545ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/100        344386 ns       344523 ns         1991 RowInvRate=344.523ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/100      1182528 ns      1182488 ns          589 RowInvRate=118.249ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/100     9300951 ns      9300832 ns           75 RowInvRate=93.0083ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/1000       719647 ns       719779 ns          967 RowInvRate=719.779ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/1000     1541497 ns      1541496 ns          456 RowInvRate=154.15ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/1000    9520791 ns      9520727 ns           73 RowInvRate=95.2073ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/1000      1411434 ns      1411576 ns          495 RowInvRate=1.41158us
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/1000     2232143 ns      2232078 ns          312 RowInvRate=223.208ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/1000   10310775 ns     10310689 ns           68 RowInvRate=103.107ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/1000      2761463 ns      2761626 ns          255 RowInvRate=2.76163us
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/1000     3604707 ns      3604733 ns          195 RowInvRate=360.473ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/1000   12153707 ns     12153632 ns           57 RowInvRate=121.536ns
BM_AddTimeunitsCalendarWeekdayCalendar/1000/2          28902328 ns     28902577 ns           24 RowInvRate=28.9026us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/2        289644395 ns    289644517 ns            2 RowInvRate=28.9645us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/2      2896232594 ns   2896206693 ns            1 RowInvRate=28.9621us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/4          28967254 ns     28966699 ns           24 RowInvRate=28.9667us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/4        289679428 ns    289676481 ns            2 RowInvRate=28.9676us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/4      2876301980 ns   2876222363 ns            1 RowInvRate=28.7622us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/8          28838700 ns     28837996 ns           24 RowInvRate=28.838us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/8        287877730 ns    287874628 ns            2 RowInvRate=28.7875us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/8      2873019623 ns   2872977165 ns            1 RowInvRate=28.7298us
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
    std::optional<std::string> serialized_calendar = to_base64_encoded_string(calendar_proto, 1LL << 30, true);
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
