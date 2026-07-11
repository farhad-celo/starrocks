#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

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

/*
2025-07-11T14:35:28+00:00
Running ./be/build_Release/src/bench/celonis/output/in_calendar_bench
Run on (32 X 3006.7 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 31.86, 24.05, 13.07
// Args: Number of rows / Number of calendar entries (ignored for weekday) / Number of calendar ids / Use weekday calendar
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
// FactoryCalendar
BM_InCalendar/1000/10/2/0          64252 ns        64228 ns        10889 RowInvRate=64.2278ns
BM_InCalendar/10000/10/2/0        623580 ns       623518 ns         1116 RowInvRate=62.3518ns
BM_InCalendar/100000/10/2/0      6188232 ns      6186779 ns          113 RowInvRate=61.8678ns
BM_InCalendar/1000/100/2/0         65848 ns        65798 ns        10641 RowInvRate=65.7977ns
BM_InCalendar/10000/100/2/0       638811 ns       638719 ns         1094 RowInvRate=63.8719ns
BM_InCalendar/100000/100/2/0     6344859 ns      6344004 ns          109 RowInvRate=63.44ns
BM_InCalendar/1000/1000/2/0        67392 ns        67184 ns        10412 RowInvRate=67.1842ns
BM_InCalendar/10000/1000/2/0      641452 ns       641369 ns         1083 RowInvRate=64.1369ns
BM_InCalendar/100000/1000/2/0    6355974 ns      6355210 ns          110 RowInvRate=63.5521ns
BM_InCalendar/1000/10/4/0          64374 ns        64339 ns        11031 RowInvRate=64.3391ns
BM_InCalendar/10000/10/4/0        622387 ns       622363 ns         1123 RowInvRate=62.2363ns
BM_InCalendar/100000/10/4/0      6182991 ns      6182927 ns          113 RowInvRate=61.8293ns
BM_InCalendar/1000/100/4/0         64814 ns        64749 ns        10798 RowInvRate=64.7486ns
BM_InCalendar/10000/100/4/0       627103 ns       627042 ns         1118 RowInvRate=62.7042ns
BM_InCalendar/100000/100/4/0     6206725 ns      6206574 ns          112 RowInvRate=62.0657ns
BM_InCalendar/1000/1000/4/0        66528 ns        66276 ns        10534 RowInvRate=66.2762ns
BM_InCalendar/10000/1000/4/0      633608 ns       633462 ns         1100 RowInvRate=63.3462ns
BM_InCalendar/100000/1000/4/0    6250123 ns      6250001 ns          111 RowInvRate=62.5ns
BM_InCalendar/1000/10/8/0          66409 ns        66385 ns        10488 RowInvRate=66.3854ns
BM_InCalendar/10000/10/8/0        647490 ns       647447 ns         1088 RowInvRate=64.7447ns
BM_InCalendar/100000/10/8/0      6380250 ns      6379632 ns          110 RowInvRate=63.7963ns
BM_InCalendar/1000/100/8/0         67649 ns        67506 ns        10358 RowInvRate=67.5064ns
BM_InCalendar/10000/100/8/0       652854 ns       652769 ns         1075 RowInvRate=65.2769ns
BM_InCalendar/100000/100/8/0     6510915 ns      6510176 ns          112 RowInvRate=65.1018ns
BM_InCalendar/1000/1000/8/0        69957 ns        69666 ns        10027 RowInvRate=69.6664ns
BM_InCalendar/10000/1000/8/0      653867 ns       653655 ns         1070 RowInvRate=65.3655ns
BM_InCalendar/100000/1000/8/0    6459604 ns      6459369 ns          106 RowInvRate=64.5937ns
// WeekdayCalendar
BM_InCalendar/1000/0/2/1          124579 ns       124557 ns         5635 RowInvRate=124.557ns
BM_InCalendar/10000/0/2/1        1228159 ns      1228102 ns          571 RowInvRate=122.81ns
BM_InCalendar/100000/0/2/1      12232851 ns     12232276 ns           57 RowInvRate=122.323ns
BM_InCalendar/1000/0/4/1          123051 ns       123031 ns         5693 RowInvRate=123.031ns
BM_InCalendar/10000/0/4/1        1215721 ns      1215608 ns          577 RowInvRate=121.561ns
BM_InCalendar/100000/0/4/1      12121826 ns     12121344 ns           58 RowInvRate=121.213ns
BM_InCalendar/1000/0/8/1          126552 ns       126531 ns         5566 RowInvRate=126.531ns
BM_InCalendar/10000/0/8/1        1218557 ns      1218496 ns          572 RowInvRate=121.85ns
BM_InCalendar/100000/0/8/1      12208510 ns     12207887 ns           57 RowInvRate=122.079ns
*/

static void BM_InCalendar(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue.
    int num_rows = state.range(0);
    int num_calendar_entries = state.range(1);
    int num_calendar_ids = state.range(2);
    bool use_weekday_calendar = state.range(3);

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

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor(TYPE_DATETIME),
                                                        TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)),
                                                        TypeDescriptor(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    celonis::accelerator::Calendar calendar_proto;
    if (use_weekday_calendar) {
        // Create WeekdayCalendar (Monday-Friday 9-5)
        celonis::accelerator::MultiWeekdayCalendar multi_weekday_calendar;
        for (auto i = 0; i < num_calendar_ids; ++i) {
            celonis::accelerator::WeekdayCalendar* weekday_cal = multi_weekday_calendar.add_calendars();
            weekday_cal->set_calendar_id(calendar_ids[i]);
            // Helper to create a weekday entry
            auto create_weekday_entry = [](bool use_day, int begin_hour = 9, int end_hour = 17) {
                celonis::accelerator::WeekdayCalendarEntry entry;
                entry.set_use_day(use_day);
                if (use_day) {
                    celonis::accelerator::WeekdayCalendarEntryShift* shift = entry.mutable_shift();
                    shift->set_begin(begin_hour * 3600 * 1000); // Convert hours to milliseconds
                    shift->set_end(end_hour * 3600 * 1000);
                }
                return entry;
            };

            // Monday-Friday: 9AM-5PM
            *weekday_cal->mutable_monday() = create_weekday_entry(true);
            *weekday_cal->mutable_tuesday() = create_weekday_entry(true);
            *weekday_cal->mutable_wednesday() = create_weekday_entry(true);
            *weekday_cal->mutable_thursday() = create_weekday_entry(true);
            *weekday_cal->mutable_friday() = create_weekday_entry(true);

            // Weekend: closed
            *weekday_cal->mutable_saturday() = create_weekday_entry(false);
            *weekday_cal->mutable_sunday() = create_weekday_entry(false);
        }

        *calendar_proto.mutable_multi_weekday_calendar() = multi_weekday_calendar;
    } else {
        celonis::accelerator::FactoryCalendar factory_calendar;
        for (auto i = 0; i < num_calendar_ids; ++i) {
            std::string calendar_id = calendar_ids[i];
            for (auto j = 0; j < num_calendar_entries; ++j) {
                celonis::accelerator::FactoryCalendarEntry* entry = factory_calendar.add_entries();
                int unix_seconds = uniform_timestamp_increase(rng);
                entry->set_start_date(unix_seconds * 1000);
                entry->set_end_date((unix_seconds + 3600) * 1000);
                entry->set_calendar_id(calendar_id);
            }
        }
        *calendar_proto.mutable_factory_calendar() = factory_calendar;
    }

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
        auto calendar_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto calendar_id_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        calendar_column->append_datum(calendar_array);
        calendar_column = ConstColumn::create(calendar_column, num_rows);

        for (int i = 0; i < num_rows; i++) {
            int unix_seconds = uniform_timestamp_increase(rng);
            timestamp.from_unix_second(unix_seconds);
            timestamp_column->append_datum(timestamp);
            calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
        }

        ctx->set_constant_columns({nullptr, calendar_column, nullptr});

        ASSERT_OK(CelonisTimeFunctions::in_calendar_prepare(ctx.get(),
                                                            FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::in_calendar_prepare(ctx.get(),
                                                            FunctionContext::FunctionStateScope::THREAD_LOCAL));
        state.ResumeTiming();

        EXPECT_TRUE(
                CelonisTimeFunctions::in_calendar(ctx.get(), {timestamp_column, calendar_column, calendar_id_column})
                        .ok());

        state.PauseTiming();
        ASSERT_OK(
                CelonisTimeFunctions::in_calendar_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::in_calendar_close(ctx.get(),
                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        state.ResumeTiming();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of calendar entries (ignored for weekday) / Number of calendar ids / Use weekday calendar
// Factory calendar benchmarks
BENCHMARK(BM_InCalendar)->ArgsProduct({{1000, 10000, 100000}, {10, 100, 1000}, {2, 4, 8}, {0}});

// Weekday calendar benchmarks (calendar entries parameter is ignored)
BENCHMARK(BM_InCalendar)->ArgsProduct({{1000, 10000, 100000}, {0}, {2, 4, 8}, {1}});

} // namespace starrocks

BENCHMARK_MAIN();
