#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"
#include "modules/query/calendars.pb.h"

namespace starrocks {

/*
2024-12-28T21:05:33+00:00
Running ./be/build_Release/src/bench/celonis/output/timeunits_between_calendar_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.17, 9.07, 18.34
// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
----------------------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------
BM_TimeunitsBetweenCalendar/1000/10/2         126377 ns       126380 ns         5559 RowInvRate=126.38ns
BM_TimeunitsBetweenCalendar/10000/10/2       1168915 ns      1168859 ns          600 RowInvRate=116.886ns
BM_TimeunitsBetweenCalendar/100000/10/2     11486682 ns     11486436 ns           61 RowInvRate=114.864ns
BM_TimeunitsBetweenCalendar/1000/100/2        172233 ns       172234 ns         3985 RowInvRate=172.234ns
BM_TimeunitsBetweenCalendar/10000/100/2      1195836 ns      1195750 ns          584 RowInvRate=119.575ns
BM_TimeunitsBetweenCalendar/100000/100/2    11049588 ns     11049684 ns           63 RowInvRate=110.497ns
BM_TimeunitsBetweenCalendar/1000/1000/2       711217 ns       711308 ns          959 RowInvRate=711.308ns
BM_TimeunitsBetweenCalendar/10000/1000/2     1749006 ns      1748960 ns          397 RowInvRate=174.896ns
BM_TimeunitsBetweenCalendar/100000/1000/2   11569324 ns     11568437 ns           61 RowInvRate=115.684ns
BM_TimeunitsBetweenCalendar/1000/10/4         127278 ns       127280 ns         5494 RowInvRate=127.28ns
BM_TimeunitsBetweenCalendar/10000/10/4       1101921 ns      1101817 ns          632 RowInvRate=110.182ns
BM_TimeunitsBetweenCalendar/100000/10/4     10688371 ns     10688435 ns           65 RowInvRate=106.884ns
BM_TimeunitsBetweenCalendar/1000/100/4        234271 ns       234287 ns         2997 RowInvRate=234.287ns
BM_TimeunitsBetweenCalendar/10000/100/4      1241178 ns      1241090 ns          565 RowInvRate=124.109ns
BM_TimeunitsBetweenCalendar/100000/100/4    10836962 ns     10836879 ns           64 RowInvRate=108.369ns
BM_TimeunitsBetweenCalendar/1000/1000/4      1329750 ns      1329811 ns          528 RowInvRate=1.32981us
BM_TimeunitsBetweenCalendar/10000/1000/4     2295288 ns      2295199 ns          306 RowInvRate=229.52ns
BM_TimeunitsBetweenCalendar/100000/1000/4   12054243 ns     12054229 ns           58 RowInvRate=120.542ns
BM_TimeunitsBetweenCalendar/1000/10/8         159357 ns       159356 ns         4393 RowInvRate=159.356ns
BM_TimeunitsBetweenCalendar/10000/10/8       1127201 ns      1127166 ns          622 RowInvRate=112.717ns
BM_TimeunitsBetweenCalendar/100000/10/8     10779721 ns     10779354 ns           65 RowInvRate=107.794ns
BM_TimeunitsBetweenCalendar/1000/100/8        413398 ns       413426 ns         1689 RowInvRate=413.426ns
BM_TimeunitsBetweenCalendar/10000/100/8      1386078 ns      1386019 ns          503 RowInvRate=138.602ns
BM_TimeunitsBetweenCalendar/100000/100/8    11139132 ns     11139125 ns           62 RowInvRate=111.391ns
BM_TimeunitsBetweenCalendar/1000/1000/8      2548045 ns      2548039 ns          274 RowInvRate=2.54804us
BM_TimeunitsBetweenCalendar/10000/1000/8     3556017 ns      3555695 ns          197 RowInvRate=355.569ns
BM_TimeunitsBetweenCalendar/100000/1000/8   13613506 ns     13612980 ns           53 RowInvRate=136.13ns
*/

static void BM_TimeunitsBetweenCalendar(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue.
    int num_rows = state.range(0);
    int num_calendar_entries = state.range(1);
    int num_calendar_ids = state.range(2);

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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    celonis::accelerator::FactoryCalendar factory_calendar;
    for (auto i = 0; i < num_calendar_ids; ++i) {
        std::string calendar_id = calendar_ids[i];
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
        auto from_timestamp_column =
                ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto to_timestamp_column =
            ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto unit_column =
                ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto calendar_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto calendar_id_column =
                ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
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
        ASSERT_OK(CelonisTimeFunctions::timeunits_between_calendar_prepare(ctx.get(),
                                                                           FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::timeunits_between_calendar_prepare(ctx.get(),
                                                                         FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::timeunits_between_calendar(ctx.get(),
                                                                     {from_timestamp_column, to_timestamp_column,
                                                                      unit_column, calendar_column,
                                                                      calendar_id_column}).ok());
        ASSERT_OK(CelonisTimeFunctions::timeunits_between_calendar_close(ctx.get(),
                                                                         FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::timeunits_between_calendar_close(ctx.get(),
                                                                       FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
BENCHMARK(BM_TimeunitsBetweenCalendar)->ArgsProduct({{1000, 10000, 100000}, {10, 100, 1000}, {2, 4, 8}});

} // namespace starrocks

BENCHMARK_MAIN();
