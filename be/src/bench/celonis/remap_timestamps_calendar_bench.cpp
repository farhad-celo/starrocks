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

namespace {

/*
2025-05-16T11:10:10+02:00                                                                                                                                                                                                                                                                 
Running ./be/build_Release/src/bench/celonis/output/remap_timestamps_calendar_bench                                                                                                                                                                                                       
Run on (12 X 4600.65 MHz CPU s)                                                                                                                                                                                                                                                           
CPU Caches:                                                                                                                                                                                                                                                                               
  L1 Data 32 KiB (x6)                                                                                                                                                                                                                                                                     
  L1 Instruction 32 KiB (x6)                                                                                                                                                                                                                                                              
  L2 Unified 256 KiB (x6)                                                                                                                                                                                                                                                                 
  L3 Unified 12288 KiB (x1)                                                                                                                                                                                                                                                               
Load Average: 0.99, 0.66, 1.18                                                                                                                                                                                                                                                            
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id
----------------------------------------------------------------------------------------------------------                                                                                                                                                                                
Benchmark                                                Time             CPU   Iterations UserCounters...                                                                                                                                                                                
----------------------------------------------------------------------------------------------------------                                                                                                                                                                                
BM_RemapTimestampsFactoryCalendar/1000/2/10         134055 ns       134104 ns         5421 RowInvRate=134.104ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/2/10       1273341 ns      1273399 ns          560 RowInvRate=127.34ns                                                                                                                                                                            
BM_RemapTimestampsFactoryCalendar/100000/2/10     12509660 ns     12508366 ns           56 RowInvRate=125.084ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/1000/4/10         158676 ns       158709 ns         4630 RowInvRate=158.709ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/4/10       1317403 ns      1317426 ns          509 RowInvRate=131.743ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/100000/4/10     13270709 ns     13270474 ns           53 RowInvRate=132.705ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/1000/8/10         164797 ns       164859 ns         4259 RowInvRate=164.859ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/8/10       1330745 ns      1330780 ns          524 RowInvRate=133.078ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/100000/8/10     13026719 ns     13025635 ns           54 RowInvRate=130.256ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/1000/2/100        195097 ns       195160 ns         3553 RowInvRate=195.16ns                                                                                                                                                                            
BM_RemapTimestampsFactoryCalendar/10000/2/100      1391461 ns      1391496 ns          500 RowInvRate=139.15ns                                                                                                                                                                            
BM_RemapTimestampsFactoryCalendar/100000/2/100    13395257 ns     13394855 ns           52 RowInvRate=133.949ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/1000/4/100        266135 ns       266185 ns         2613 RowInvRate=266.185ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/4/100      1520404 ns      1520305 ns          460 RowInvRate=152.031ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/100000/4/100    14197841 ns     14195811 ns           50 RowInvRate=141.958ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/1000/8/100        412138 ns       412192 ns         1703 RowInvRate=412.192ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/8/100      1700109 ns      1700029 ns          410 RowInvRate=170.003ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/100000/8/100    14449930 ns     14448438 ns           49 RowInvRate=144.484ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/1000/2/1000       776373 ns       776371 ns          901 RowInvRate=776.371ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/2/1000     2049078 ns      2049023 ns          343 RowInvRate=204.902ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/100000/2/1000   14847942 ns     14847971 ns           47 RowInvRate=148.48ns                                                                                                                                                                            
BM_RemapTimestampsFactoryCalendar/1000/4/1000      1424846 ns      1424983 ns          490 RowInvRate=1.42498us                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/4/1000     2787114 ns      2787046 ns          253 RowInvRate=278.705ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/100000/4/1000   16061655 ns     16061216 ns           44 RowInvRate=160.612ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/1000/8/1000      2774236 ns      2774337 ns          252 RowInvRate=2.77434us                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/10000/8/1000     4158871 ns      4158332 ns          167 RowInvRate=415.833ns                                                                                                                                                                           
BM_RemapTimestampsFactoryCalendar/100000/8/1000   17730774 ns     17728753 ns           39 RowInvRate=177.288ns                                                                                                                                                                           
BM_RemapTimestampsWeekdayCalendar/1000/2            116007 ns       116037 ns         6147 RowInvRate=116.037ns                                                                                                                                                                           
BM_RemapTimestampsWeekdayCalendar/10000/2          1084361 ns      1084340 ns          633 RowInvRate=108.434ns                                                                                                                                                                           
BM_RemapTimestampsWeekdayCalendar/100000/2        10719149 ns     10717324 ns           65 RowInvRate=107.173ns                                                                                                                                                                           
BM_RemapTimestampsWeekdayCalendar/1000/4            120263 ns       120300 ns         5828 RowInvRate=120.3ns                                                                                                                                                                             
BM_RemapTimestampsWeekdayCalendar/10000/4          1100859 ns      1100796 ns          638 RowInvRate=110.08ns                                                                                                                                                                            
BM_RemapTimestampsWeekdayCalendar/100000/4        10825605 ns     10825500 ns           64 RowInvRate=108.255ns                                                                                                                                                                           
BM_RemapTimestampsWeekdayCalendar/1000/8            128758 ns       128822 ns         5459 RowInvRate=128.822ns                                                                                                                                                                           
BM_RemapTimestampsWeekdayCalendar/10000/8          1087628 ns      1087619 ns          644 RowInvRate=108.762ns                                                                                                                                                                           
BM_RemapTimestampsWeekdayCalendar/100000/8        10633302 ns     10631700 ns           65 RowInvRate=106.317ns
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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT));
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
            timestamp.from_unix_second(static_cast<int64_t>(unix_seconds), 0);
            timestamp_column->append_datum(timestamp);
            calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
        }
        ctx->set_constant_columns({nullptr, unit_column, calendar_column, nullptr});
        state.ResumeTiming();
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar(
                            ctx.get(), {timestamp_column, unit_column, calendar_column, calendar_id_column})
                            .ok());
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

} // namespace

static void BM_RemapTimestampsFactoryCalendar(benchmark::State& state) {
    const int num_calendar_entries = state.range(2);

    run_benchmark(state, [&](const auto& calendar_ids, auto& rng) {
        return create_factory_calendar(calendar_ids, rng, num_calendar_entries);
    });
}

static void BM_RemapTimestampsWeekdayCalendar(benchmark::State& state) {
    run_benchmark(state, create_weekday_calendar);
}

BENCHMARK(BM_RemapTimestampsFactoryCalendar)
        ->ArgsProduct({
                {1000, 10000, 100000}, // Number of rows
                {2, 4, 8},             // Number of calendar ids
                {10, 100, 1000},       // Number of calendar entries per id
        });

BENCHMARK(BM_RemapTimestampsWeekdayCalendar)
        ->ArgsProduct({
                {1000, 10000, 100000}, // Number of rows
                {2, 4, 8},             // Number of calendar ids
        });

} // namespace starrocks

BENCHMARK_MAIN();
