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

namespace starrocks {

namespace {

using UniformInt = std::uniform_int_distribution<int32_t>;
using CreateCalendar = std::function<celonis::accelerator::Calendar(const std::vector<std::string>, std::mt19937&)>;

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
    std::optional<std::string> serialized_calendar = to_base64_encoded_string(calendar_proto);
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

/*
2025-03-10T14:44:24+01:00
Running ./be/build_Release/src/bench/celonis/output/timeunits_between_calendar_bench
Run on (12 X 4581.27 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x6)
  L1 Instruction 32 KiB (x6)
  L2 Unified 256 KiB (x6)
  L3 Unified 12288 KiB (x1)
Load Average: 2.10, 4.64, 3.91
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id (only for factory calendar)
-----------------------------------------------------------------------------------------------------------
Benchmark                                                 Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------
BM_TimeunitsBetweenFactoryCalendar/1000/2/10         105713 ns       105734 ns         6807 RowInvRate=105.734ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/10        970951 ns       970925 ns          719 RowInvRate=97.0925ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/10      9637972 ns      9637064 ns           73 RowInvRate=96.3706ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/10         108101 ns       108111 ns         6517 RowInvRate=108.111ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/10        912434 ns       912409 ns          769 RowInvRate=91.2409ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/10      8949324 ns      8948976 ns           78 RowInvRate=89.4898ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/10         129754 ns       129784 ns         5421 RowInvRate=129.784ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/10        982028 ns       981996 ns          712 RowInvRate=98.1996ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/10      9517938 ns      9517255 ns           73 RowInvRate=95.1726ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/100        152181 ns       152234 ns         4618 RowInvRate=152.234ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/100       989869 ns       989891 ns          708 RowInvRate=98.9891ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/100     9281113 ns      9280669 ns           75 RowInvRate=92.8067ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/100        205332 ns       205407 ns         3405 RowInvRate=205.407ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/100      1005624 ns      1005645 ns          679 RowInvRate=100.565ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/100     9041291 ns      9040979 ns           78 RowInvRate=90.4098ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/100        337743 ns       337826 ns         2069 RowInvRate=337.826ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/100      1207329 ns      1207389 ns          569 RowInvRate=120.739ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/100     9793984 ns      9793183 ns           71 RowInvRate=97.9318ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/1000       680891 ns       680986 ns         1029 RowInvRate=680.986ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/1000     1535563 ns      1535590 ns          456 RowInvRate=153.559ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/1000    9832547 ns      9832122 ns           70 RowInvRate=98.3212ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/1000      1261709 ns      1261794 ns          554 RowInvRate=1.26179us
BM_TimeunitsBetweenFactoryCalendar/10000/4/1000     2067444 ns      2067453 ns          337 RowInvRate=206.745ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/1000   10503442 ns     10502398 ns           68 RowInvRate=105.024ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/1000      2447360 ns      2447414 ns          285 RowInvRate=2.44741us
BM_TimeunitsBetweenFactoryCalendar/10000/8/1000     3321928 ns      3321976 ns          210 RowInvRate=332.198ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/1000   12603961 ns     12603837 ns           56 RowInvRate=126.038ns
BM_TimeunitsBetweenWeekdayCalendar/1000/2            770667 ns       770715 ns          910 RowInvRate=770.715ns
BM_TimeunitsBetweenWeekdayCalendar/10000/2          7687574 ns      7687428 ns           91 RowInvRate=768.743ns
BM_TimeunitsBetweenWeekdayCalendar/100000/2        77135699 ns     77132789 ns            9 RowInvRate=771.328ns
BM_TimeunitsBetweenWeekdayCalendar/1000/4            775949 ns       775945 ns          824 RowInvRate=775.945ns
BM_TimeunitsBetweenWeekdayCalendar/10000/4          7651627 ns      7651528 ns           90 RowInvRate=765.153ns
BM_TimeunitsBetweenWeekdayCalendar/100000/4        76134241 ns     76133203 ns            9 RowInvRate=761.332ns
BM_TimeunitsBetweenWeekdayCalendar/1000/8            786839 ns       786864 ns          891 RowInvRate=786.864ns
BM_TimeunitsBetweenWeekdayCalendar/10000/8          7690885 ns      7690651 ns           90 RowInvRate=769.065ns
BM_TimeunitsBetweenWeekdayCalendar/100000/8        77114502 ns     77110627 ns            9 RowInvRate=771.106ns
*/

static void BM_TimeunitsBetweenFactoryCalendar(benchmark::State& state) {
    const int num_calendar_entries = state.range(2);

    run_benchmark(state, [&](const auto& calendar_ids, auto& rng) {
        UniformInt uniform_timestamp_increase(1, 3600 * 24 * 365 * 10); // 10 years

        celonis::accelerator::FactoryCalendar factory_calendar;
        for (auto i = 0; i < calendar_ids.size(); ++i) {
            const auto& calendar_id = calendar_ids[i];
            for (auto j = 0; j < num_calendar_entries; ++j) {
                celonis::accelerator::FactoryCalendarEntry* entry = factory_calendar.add_entries();
                int unix_seconds = uniform_timestamp_increase(rng);
                entry->set_start_date(unix_seconds * 1000);
                entry->set_end_date((unix_seconds + 3600) * 1000); // the size of the entry is 1 hour.
                entry->set_calendar_id(calendar_id);
            }
        }

        celonis::accelerator::Calendar calendar_proto;
        *calendar_proto.mutable_factory_calendar() = factory_calendar;
        return calendar_proto;
    });
}

static void BM_TimeunitsBetweenWeekdayCalendar(benchmark::State& state) {
    run_benchmark(state, [&](const auto& calendar_ids, auto& rng) {
        celonis::accelerator::MultiWeekdayCalendar multi_weekday_calendar;
        for (auto i = 0; i < calendar_ids.size(); ++i) {
            celonis::accelerator::WeekdayCalendar* weekday_calendar = multi_weekday_calendar.add_calendars();
            const std::string& calendar_id = calendar_ids[i];
            weekday_calendar->set_calendar_id(calendar_id);

            for (celonis::accelerator::WeekdayCalendarEntry* entry : {
                         weekday_calendar->mutable_monday(),
                         weekday_calendar->mutable_tuesday(),
                         weekday_calendar->mutable_wednesday(),
                         weekday_calendar->mutable_thursday(),
                         weekday_calendar->mutable_friday(),
                 }) {
                entry->set_use_day(true);
                entry->mutable_shift()->set_begin(0);
                entry->mutable_shift()->set_end(86400000);
            }
            for (celonis::accelerator::WeekdayCalendarEntry* entry : {
                         weekday_calendar->mutable_saturday(),
                         weekday_calendar->mutable_sunday(),
                 }) {
                entry->set_use_day(false);
            }
        }

        celonis::accelerator::Calendar calendar_proto;
        *calendar_proto.mutable_multi_weekday_calendar() = multi_weekday_calendar;
        return calendar_proto;
    });
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
