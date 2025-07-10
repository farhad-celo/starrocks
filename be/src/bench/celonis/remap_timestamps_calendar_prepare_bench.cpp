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
2025-07-10T09:30:02+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_timestamps_calendar_prepare_bench
Run on (32 X 3102.13 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 38.54, 32.75, 14.35
// Args: Number of rows / Number of calendar ids / Number of calendar entries per id
----------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                        Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------------------------------------
BM_RemapTimestampsFactoryCalendarPrepare/4096/256/8096/iterations:10     812662170 ns    812638875 ns           10 PrepareCyclesPerSecond=1.23056/s
BM_RemapTimestampsFactoryCalendarPrepare/40960/256/8096/iterations:10    825079035 ns    825066480 ns           10 PrepareCyclesPerSecond=1.21202/s
BM_RemapTimestampsFactoryCalendarPrepare/409600/256/8096/iterations:10   827323041 ns    827312261 ns           10 PrepareCyclesPerSecond=1.20873/s
BM_RemapTimestampsFactoryCalendarPrepare/4096/512/8096/iterations:10    1655994064 ns   1655954785 ns           10 PrepareCyclesPerSecond=0.603881/s
BM_RemapTimestampsFactoryCalendarPrepare/40960/512/8096/iterations:10   1702450509 ns   1702353112 ns           10 PrepareCyclesPerSecond=0.587422/s
BM_RemapTimestampsFactoryCalendarPrepare/409600/512/8096/iterations:10  1704191911 ns   1704087332 ns           10 PrepareCyclesPerSecond=0.586824/s
BM_RemapTimestampsFactoryCalendarPrepare/4096/1024/8096/iterations:10   3444403169 ns   3444221390 ns           10 PrepareCyclesPerSecond=0.290341/s
BM_RemapTimestampsFactoryCalendarPrepare/40960/1024/8096/iterations:10  3572983118 ns   3572609423 ns           10 PrepareCyclesPerSecond=0.279907/s
BM_RemapTimestampsFactoryCalendarPrepare/409600/1024/8096/iterations:10 3534062098 ns   3533742308 ns           10 PrepareCyclesPerSecond=0.282986/s
*/

namespace {

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

    // --- Create columns once outside the main loop to avoid repeated allocation ---
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
        TimestampValue timestamp;
        int unix_seconds = uniform_timestamp_increase(rng);
        timestamp.from_unix_second(static_cast<int64_t>(unix_seconds), 0);
        timestamp_column->append_datum(timestamp);
        calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
    }
    ctx->set_constant_columns({nullptr, unit_column, calendar_column, nullptr});

    for (auto _ : state) {
        state.ResumeTiming();
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_close(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        state.PauseTiming();
    }
    state.counters["PrepareCyclesPerSecond"] =
            benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

} // namespace

static void BM_RemapTimestampsFactoryCalendarPrepare(benchmark::State& state) {
    const int num_calendar_entries = state.range(2);

    run_benchmark(state, [&](const auto& calendar_ids, auto& rng) {
        return create_factory_calendar(calendar_ids, rng, num_calendar_entries);
    });
}

BENCHMARK(BM_RemapTimestampsFactoryCalendarPrepare)
        ->ArgsProduct({
                {4096, 40960, 409600},       // Number of rows
                {256, 512, 1024},            // Number of calendar ids
                {8096},                      // Number of calendar entries per id
        })->Iterations(10);

} // namespace starrocks

BENCHMARK_MAIN();
