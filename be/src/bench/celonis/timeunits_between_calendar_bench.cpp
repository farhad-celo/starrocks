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
Running ./be/build_Release/src/bench/celonis/output/timeunits_between_calendar_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.40, 2.49, 1.45
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id (only for factory calendar)
-----------------------------------------------------------------------------------------------------------
Benchmark                                                 Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------
BM_TimeunitsBetweenFactoryCalendar/1000/2/10          80196 ns        80218 ns         8737 RowInvRate=80.2183ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/10        695704 ns       695668 ns         1007 RowInvRate=69.5668ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/10      6776333 ns      6775957 ns          103 RowInvRate=67.7596ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/10          90822 ns        90838 ns         7728 RowInvRate=90.8383ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/10        706973 ns       706951 ns          990 RowInvRate=70.6951ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/10      6813235 ns      6813018 ns          103 RowInvRate=68.1302ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/10         107382 ns       107419 ns         6508 RowInvRate=107.419ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/10        742425 ns       742380 ns          941 RowInvRate=74.238ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/10      6995538 ns      6995199 ns           99 RowInvRate=69.952ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/100        137812 ns       137866 ns         5060 RowInvRate=137.866ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/100       817302 ns       817269 ns          856 RowInvRate=81.7269ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/100     7505285 ns      7505170 ns           91 RowInvRate=75.0517ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/100        195228 ns       195334 ns         3609 RowInvRate=195.334ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/100       831537 ns       831540 ns          856 RowInvRate=83.154ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/100     6954055 ns      6953275 ns           98 RowInvRate=69.5328ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/100        335389 ns       335518 ns         2123 RowInvRate=335.518ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/100       971952 ns       971950 ns          714 RowInvRate=97.195ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/100     7252716 ns      7252769 ns           95 RowInvRate=72.5277ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/1000       711242 ns       711380 ns          983 RowInvRate=711.38ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/1000     1403737 ns      1403716 ns          498 RowInvRate=140.372ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/1000    8108971 ns      8108722 ns           86 RowInvRate=81.0872ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/1000      1388549 ns      1388651 ns          504 RowInvRate=1.38865us
BM_TimeunitsBetweenFactoryCalendar/10000/4/1000     2021836 ns      2021840 ns          346 RowInvRate=202.184ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/1000    8268678 ns      8268579 ns           85 RowInvRate=82.6858ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/1000      2718563 ns      2718669 ns          257 RowInvRate=2.71867us
BM_TimeunitsBetweenFactoryCalendar/10000/8/1000     3370567 ns      3370593 ns          208 RowInvRate=337.059ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/1000    9990723 ns      9990366 ns           70 RowInvRate=99.9037ns
BM_TimeunitsBetweenWeekdayCalendar/1000/2            201625 ns       201642 ns         3472 RowInvRate=201.642ns
BM_TimeunitsBetweenWeekdayCalendar/10000/2          1945042 ns      1944971 ns          361 RowInvRate=194.497ns
BM_TimeunitsBetweenWeekdayCalendar/100000/2        19257949 ns     19256994 ns           36 RowInvRate=192.57ns
BM_TimeunitsBetweenWeekdayCalendar/1000/4            204885 ns       204901 ns         3410 RowInvRate=204.901ns
BM_TimeunitsBetweenWeekdayCalendar/10000/4          1935651 ns      1935604 ns          362 RowInvRate=193.56ns
BM_TimeunitsBetweenWeekdayCalendar/100000/4        19239282 ns     19238288 ns           36 RowInvRate=192.383ns
BM_TimeunitsBetweenWeekdayCalendar/1000/8            216052 ns       216079 ns         3239 RowInvRate=216.079ns
BM_TimeunitsBetweenWeekdayCalendar/10000/8          1911172 ns      1911085 ns          366 RowInvRate=191.108ns
BM_TimeunitsBetweenWeekdayCalendar/100000/8        18915087 ns     18913462 ns           37 RowInvRate=189.135ns
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
