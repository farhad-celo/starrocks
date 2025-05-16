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

/*
2025-05-16T11:11:57+02:00
Running ./be/build_Release/src/bench/celonis/output/timeunits_between_calendar_bench
Run on (12 X 4695.25 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x6)
  L1 Instruction 32 KiB (x6)
  L2 Unified 256 KiB (x6)
  L3 Unified 12288 KiB (x1)
Load Average: 1.12, 0.83, 1.18
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id (only for factory calendar)
-----------------------------------------------------------------------------------------------------------
Benchmark                                                 Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------
BM_TimeunitsBetweenFactoryCalendar/1000/2/10         111927 ns       111999 ns         6422 RowInvRate=111.999ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/10       1022838 ns      1022880 ns          679 RowInvRate=102.288ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/10     10055444 ns     10055171 ns           69 RowInvRate=100.552ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/10         117417 ns       117501 ns         5953 RowInvRate=117.501ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/10        999740 ns       999783 ns          700 RowInvRate=99.9783ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/10      9734820 ns      9734373 ns           72 RowInvRate=97.3437ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/10         138560 ns       138651 ns         5040 RowInvRate=138.651ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/10       1061729 ns      1061791 ns          659 RowInvRate=106.179ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/10     10168503 ns     10168307 ns           69 RowInvRate=101.683ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/100        165712 ns       165810 ns         4212 RowInvRate=165.81ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/100      1083170 ns      1083219 ns          648 RowInvRate=108.322ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/100    10212119 ns     10211617 ns           68 RowInvRate=102.116ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/100        226522 ns       226642 ns         3082 RowInvRate=226.642ns
BM_TimeunitsBetweenFactoryCalendar/10000/4/100      1143458 ns      1143477 ns          597 RowInvRate=114.348ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/100    10024156 ns     10023964 ns           68 RowInvRate=100.24ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/100        369653 ns       369775 ns         1883 RowInvRate=369.775ns
BM_TimeunitsBetweenFactoryCalendar/10000/8/100      1298053 ns      1298095 ns          535 RowInvRate=129.809ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/100    10547513 ns     10546898 ns           66 RowInvRate=105.469ns
BM_TimeunitsBetweenFactoryCalendar/1000/2/1000       733089 ns       733179 ns          926 RowInvRate=733.179ns
BM_TimeunitsBetweenFactoryCalendar/10000/2/1000     1733089 ns      1733070 ns          423 RowInvRate=173.307ns
BM_TimeunitsBetweenFactoryCalendar/100000/2/1000   10646752 ns     10646457 ns           65 RowInvRate=106.465ns
BM_TimeunitsBetweenFactoryCalendar/1000/4/1000      1365738 ns      1365730 ns          516 RowInvRate=1.36573us
BM_TimeunitsBetweenFactoryCalendar/10000/4/1000     2270178 ns      2270200 ns          308 RowInvRate=227.02ns
BM_TimeunitsBetweenFactoryCalendar/100000/4/1000   11191617 ns     11189452 ns           62 RowInvRate=111.895ns
BM_TimeunitsBetweenFactoryCalendar/1000/8/1000      2616783 ns      2616776 ns          266 RowInvRate=2.61678us
BM_TimeunitsBetweenFactoryCalendar/10000/8/1000     3594399 ns      3594444 ns          193 RowInvRate=359.444ns
BM_TimeunitsBetweenFactoryCalendar/100000/8/1000   13165963 ns     13165233 ns           54 RowInvRate=131.652ns
BM_TimeunitsBetweenWeekdayCalendar/1000/2            237860 ns       237920 ns         2936 RowInvRate=237.92ns
BM_TimeunitsBetweenWeekdayCalendar/10000/2          2312957 ns      2312974 ns          303 RowInvRate=231.297ns
BM_TimeunitsBetweenWeekdayCalendar/100000/2        22819305 ns     22818755 ns           30 RowInvRate=228.188ns
BM_TimeunitsBetweenWeekdayCalendar/1000/4            242526 ns       242574 ns         2879 RowInvRate=242.574ns
BM_TimeunitsBetweenWeekdayCalendar/10000/4          2315405 ns      2315278 ns          303 RowInvRate=231.528ns
BM_TimeunitsBetweenWeekdayCalendar/100000/4        22865068 ns     22864003 ns           30 RowInvRate=228.64ns
BM_TimeunitsBetweenWeekdayCalendar/1000/8            256910 ns       256982 ns         2726 RowInvRate=256.982ns
BM_TimeunitsBetweenWeekdayCalendar/10000/8          2364146 ns      2364158 ns          296 RowInvRate=236.416ns
BM_TimeunitsBetweenWeekdayCalendar/100000/8        23338138 ns     23337595 ns           30 RowInvRate=233.376ns
*/

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
