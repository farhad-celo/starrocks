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
2025-09-15T17:58:41+00:00
Running ./be/build_Release/src/bench/celonis/output/add_timeunits_calendar_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.06, 2.67, 1.86
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id
---------------------------------------------------------------------------------------------------------------
Benchmark                                                     Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------------------
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/10          91933 ns        91954 ns         7606 RowInvRate=91.9543ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/10        809848 ns       809794 ns          864 RowInvRate=80.9794ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/10      7925070 ns      7924788 ns           88 RowInvRate=79.2479ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/10         102542 ns       102574 ns         6812 RowInvRate=102.574ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/10        823768 ns       823754 ns          851 RowInvRate=82.3754ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/10      7938054 ns      7937484 ns           88 RowInvRate=79.3748ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/10         145787 ns       145823 ns         4795 RowInvRate=145.823ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/10       1127439 ns      1127404 ns          595 RowInvRate=112.74ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/10     11288423 ns     11288421 ns           61 RowInvRate=112.884ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/100        142564 ns       142611 ns         4887 RowInvRate=142.611ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/100       869331 ns       869281 ns          805 RowInvRate=86.9281ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/100     8041973 ns      8041995 ns           87 RowInvRate=80.4199ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/100        201769 ns       201846 ns         3469 RowInvRate=201.846ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/100       932587 ns       932579 ns          751 RowInvRate=93.2579ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/100     8078268 ns      8078297 ns           87 RowInvRate=80.783ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/100        365291 ns       365398 ns         1906 RowInvRate=365.398ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/100      1205711 ns      1205701 ns          512 RowInvRate=120.57ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/100    11535815 ns     11535548 ns           72 RowInvRate=115.355ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/1000       732471 ns       732539 ns          954 RowInvRate=732.539ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/1000     1469838 ns      1469839 ns          477 RowInvRate=146.984ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/1000    8718028 ns      8717887 ns           81 RowInvRate=87.1789ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/1000      1430180 ns      1430295 ns          490 RowInvRate=1.43029us
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/1000     2168418 ns      2168412 ns          321 RowInvRate=216.841ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/1000    9524223 ns      9524004 ns           74 RowInvRate=95.24ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/1000      2807464 ns      2807529 ns          248 RowInvRate=2.80753us
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/1000     3823380 ns      3823425 ns          182 RowInvRate=382.342ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/1000   14375411 ns     14375017 ns           48 RowInvRate=143.75ns
BM_AddTimeunitsCalendarWeekdayCalendar/1000/2          29070320 ns     29069959 ns           24 RowInvRate=29.07us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/2        288619473 ns    288607663 ns            2 RowInvRate=28.8608us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/2      2887074301 ns   2887013212 ns            1 RowInvRate=28.8701us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/4          28902045 ns     28900715 ns           24 RowInvRate=28.9007us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/4        287845774 ns    287833154 ns            2 RowInvRate=28.7833us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/4      2891837590 ns   2891735059 ns            1 RowInvRate=28.9174us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/8          28905433 ns     28904104 ns           24 RowInvRate=28.9041us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/8        289465571 ns    289458524 ns            2 RowInvRate=28.9459us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/8      2904458347 ns   2904338998 ns            1 RowInvRate=29.0434us
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
            TypeDescriptor(TYPE_DATETIME), TypeDescriptor(TYPE_BIGINT), TypeDescriptor(TYPE_VARCHAR),
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), TypeDescriptor(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor(TYPE_DATETIME);
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
