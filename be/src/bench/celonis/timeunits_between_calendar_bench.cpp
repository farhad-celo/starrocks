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
2025-06-04T16:52:30+00:00
Running ./be/build_Release/src/bench/celonis/output/timeunits_between_calendar_bench
Run on (32 X 3106.52 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 10.10, 5.84, 5.83
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id (only for factory calendar)
-----------------------------------------------------------------------------------------------------------
Benchmark                                                 Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------
BM_TimeunitsBetweenFactoryCalendar/1000/2/10          89627 ns        89636 ns         7816 RowInvRate=89.6362ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/10        790762 ns       790725 ns          891 RowInvRate=79.0725ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/10      7755894 ns      7755797 ns           90 RowInvRate=77.558ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/10          89619 ns        89633 ns         7874 RowInvRate=89.6332ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/10        730956 ns       730920 ns          961 RowInvRate=73.092ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/10      7036221 ns      7035927 ns           99 RowInvRate=70.3593ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/10         109647 ns       109665 ns         6365 RowInvRate=109.665ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/10        787424 ns       787414 ns          885 RowInvRate=78.7414ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/10      7502329 ns      7502262 ns           93 RowInvRate=75.0226ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/100        138655 ns       138684 ns         5048 RowInvRate=138.684ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/100       843626 ns       843611 ns          833 RowInvRate=84.3611ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/100     7848409 ns      7848173 ns           90 RowInvRate=78.4817ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/100        193176 ns       193241 ns         3615 RowInvRate=193.241ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/100       833064 ns       833036 ns          831 RowInvRate=83.3036ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/100     7135881 ns      7135610 ns           98 RowInvRate=71.3561ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/100        330295 ns       330387 ns         2150 RowInvRate=330.387ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/100      1008600 ns      1008596 ns          702 RowInvRate=100.86ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/100     7916410 ns      7916377 ns           87 RowInvRate=79.1638ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/1000       651915 ns       651991 ns         1071 RowInvRate=651.991ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/1000     1373167 ns      1373170 ns          510 RowInvRate=137.317ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/1000    8364051 ns      8364099 ns           83 RowInvRate=83.641ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/1000      1294872 ns      1294921 ns          541 RowInvRate=1.29492us
BM_TimeunitsBetweenFactoryCalendar/10000/4/1000     1925116 ns      1925088 ns          366 RowInvRate=192.509ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/1000    8375597 ns      8374999 ns           83 RowInvRate=83.75ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/1000      2510189 ns      2510232 ns          281 RowInvRate=2.51023us
BM_TimeunitsBetweenFactoryCalendar/10000/8/1000     3192811 ns      3192760 ns          218 RowInvRate=319.276ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/1000   10211777 ns     10211424 ns           70 RowInvRate=102.114ns
BM_TimeunitsBetweenWeekdayCalendar/1000/2            242226 ns       242255 ns         2880 RowInvRate=242.255ns
BM_TimeunitsBetweenWeekdayCalendar/10000/2          2368772 ns      2368739 ns          296 RowInvRate=236.874ns
BM_TimeunitsBetweenWeekdayCalendar/100000/2        23473757 ns     23473624 ns           30 RowInvRate=234.736ns
BM_TimeunitsBetweenWeekdayCalendar/1000/4            245554 ns       245586 ns         2856 RowInvRate=245.586ns
BM_TimeunitsBetweenWeekdayCalendar/10000/4          2345966 ns      2345925 ns          300 RowInvRate=234.593ns
BM_TimeunitsBetweenWeekdayCalendar/100000/4        23261593 ns     23260890 ns           30 RowInvRate=232.609ns
BM_TimeunitsBetweenWeekdayCalendar/1000/8            259098 ns       259124 ns         2710 RowInvRate=259.124ns
BM_TimeunitsBetweenWeekdayCalendar/10000/8          2390174 ns      2390082 ns          292 RowInvRate=239.008ns
BM_TimeunitsBetweenWeekdayCalendar/100000/8        23795948 ns     23795556 ns           30 RowInvRate=237.956ns
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
