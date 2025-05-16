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
2025-05-16T11:10:56+02:00                                                                                                                                                                                                                                                                 
Running ./be/build_Release/src/bench/celonis/output/add_timeunits_calendar_bench                                                                                                                                                                                                          
Run on (12 X 4700.7 MHz CPU s)                                                                                                                                                                                                                                                            
CPU Caches:                                                                                                                                                                                                                                                                               
  L1 Data 32 KiB (x6)                                                                                                                                                                                                                                                                     
  L1 Instruction 32 KiB (x6)                                                                                                                                                                                                                                                              
  L2 Unified 256 KiB (x6)                                                                                                                                                                                                                                                                 
  L3 Unified 12288 KiB (x1)                                                                                                                                                                                                                                                               
Load Average: 1.23, 0.77, 1.19                                                                                                                                                                                                                                                            
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id
---------------------------------------------------------------------------------------------------------------                                                                                                                                                                           
Benchmark                                                     Time             CPU   Iterations UserCounters...                                                                                                                                                                           
---------------------------------------------------------------------------------------------------------------                                                                                                                                                                           
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/10         113886 ns       113942 ns         6335 RowInvRate=113.942ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/10       1032588 ns      1032645 ns          678 RowInvRate=103.264ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/10     10156322 ns     10155913 ns           69 RowInvRate=101.559ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/10         122963 ns       123017 ns         5682 RowInvRate=123.017ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/10       1039898 ns      1039933 ns          673 RowInvRate=103.993ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/10     10149681 ns     10148926 ns           68 RowInvRate=101.489ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/10         142249 ns       142341 ns         4904 RowInvRate=142.341ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/10       1063176 ns      1063223 ns          658 RowInvRate=106.322ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/10     10196136 ns     10196156 ns           68 RowInvRate=101.962ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/100        174423 ns       174529 ns         4038 RowInvRate=174.529ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/100      1099226 ns      1099236 ns          639 RowInvRate=109.924ns                                                                                                                                                                      
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/100    10344215 ns     10343913 ns           67 RowInvRate=103.439ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/100        251671 ns       251760 ns         2844 RowInvRate=251.76ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/100      1195625 ns      1195675 ns          557 RowInvRate=119.567ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/100    10414709 ns     10414188 ns           67 RowInvRate=104.142ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/100        403627 ns       403754 ns         1770 RowInvRate=403.754ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/100      1345838 ns      1345880 ns          513 RowInvRate=134.588ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/100    10578753 ns     10578577 ns           66 RowInvRate=105.786ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/2/1000       796926 ns       797044 ns          875 RowInvRate=797.044ns
BM_AddTimeunitsCalendarFactoryCalendar/10000/2/1000     1732585 ns      1732696 ns          404 RowInvRate=173.27ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/2/1000   10960622 ns     10960277 ns           63 RowInvRate=109.603ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/4/1000      1497528 ns      1497547 ns          469 RowInvRate=1.49755us
BM_AddTimeunitsCalendarFactoryCalendar/10000/4/1000     2435017 ns      2434945 ns          288 RowInvRate=243.495ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/4/1000   11739086 ns     11738514 ns           59 RowInvRate=117.385ns
BM_AddTimeunitsCalendarFactoryCalendar/1000/8/1000      2848345 ns      2848369 ns          246 RowInvRate=2.84837us
BM_AddTimeunitsCalendarFactoryCalendar/10000/8/1000     3828168 ns      3828264 ns          183 RowInvRate=382.826ns
BM_AddTimeunitsCalendarFactoryCalendar/100000/8/1000   13440837 ns     13440501 ns           52 RowInvRate=134.405ns
BM_AddTimeunitsCalendarWeekdayCalendar/1000/2          43431229 ns     43430059 ns           16 RowInvRate=43.4301us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/2        436462360 ns    436449986 ns            2 RowInvRate=43.645us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/2      4351231653 ns   4351085865 ns            1 RowInvRate=43.5109us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/4          43701228 ns     43700120 ns           16 RowInvRate=43.7001us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/4        436352147 ns    436341835 ns            2 RowInvRate=43.6342us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/4      4393160402 ns   4392990169 ns            1 RowInvRate=43.9299us
BM_AddTimeunitsCalendarWeekdayCalendar/1000/8          44141921 ns     44140979 ns           16 RowInvRate=44.141us
BM_AddTimeunitsCalendarWeekdayCalendar/10000/8        436895257 ns    436884023 ns            2 RowInvRate=43.6884us
BM_AddTimeunitsCalendarWeekdayCalendar/100000/8      4405708032 ns   4405569630 ns            1 RowInvRate=44.0557us
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
