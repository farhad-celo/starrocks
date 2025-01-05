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
2025-01-03T17:50:14+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_timestamps_calendar_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.87, 27.73, 31.23
// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_RemapTimestamps/1000/10/2         141986 ns       142007 ns         4875 RowInvRate=142.007ns
BM_RemapTimestamps/10000/10/2       1290712 ns      1290679 ns          540 RowInvRate=129.068ns
BM_RemapTimestamps/100000/10/2     12735974 ns     12735881 ns           56 RowInvRate=127.359ns
BM_RemapTimestamps/1000/100/2        208665 ns       208693 ns         3360 RowInvRate=208.693ns
BM_RemapTimestamps/10000/100/2      1449871 ns      1449828 ns          484 RowInvRate=144.983ns
BM_RemapTimestamps/100000/100/2    13740936 ns     13740675 ns           51 RowInvRate=137.407ns
BM_RemapTimestamps/1000/1000/2       726523 ns       726626 ns         1003 RowInvRate=726.626ns
BM_RemapTimestamps/10000/1000/2     2067462 ns      2067394 ns          347 RowInvRate=206.739ns
BM_RemapTimestamps/100000/1000/2   14982789 ns     14982095 ns           47 RowInvRate=149.821ns
BM_RemapTimestamps/1000/10/4         153518 ns       153528 ns         4591 RowInvRate=153.528ns
BM_RemapTimestamps/10000/10/4       1338535 ns      1338449 ns          526 RowInvRate=133.845ns
BM_RemapTimestamps/100000/10/4     13157084 ns     13156701 ns           53 RowInvRate=131.567ns
BM_RemapTimestamps/1000/100/4        265724 ns       265769 ns         2693 RowInvRate=265.769ns
BM_RemapTimestamps/10000/100/4      1509473 ns      1509441 ns          463 RowInvRate=150.944ns
BM_RemapTimestamps/100000/100/4    13973129 ns     13972908 ns           50 RowInvRate=139.729ns
BM_RemapTimestamps/1000/1000/4      1363237 ns      1363315 ns          524 RowInvRate=1.36332us
BM_RemapTimestamps/10000/1000/4     2644993 ns      2644937 ns          265 RowInvRate=264.494ns
BM_RemapTimestamps/100000/1000/4   15811306 ns     15811181 ns           44 RowInvRate=158.112ns
BM_RemapTimestamps/1000/10/8         183230 ns       183245 ns         3795 RowInvRate=183.245ns
BM_RemapTimestamps/10000/10/8       1426510 ns      1426442 ns          489 RowInvRate=142.644ns
BM_RemapTimestamps/100000/10/8     13950559 ns     13950220 ns           51 RowInvRate=139.502ns
BM_RemapTimestamps/1000/100/8        429274 ns       429360 ns         1679 RowInvRate=429.36ns
BM_RemapTimestamps/10000/100/8      1715376 ns      1715318 ns          412 RowInvRate=171.532ns
BM_RemapTimestamps/100000/100/8    14484444 ns     14483870 ns           47 RowInvRate=144.839ns
BM_RemapTimestamps/1000/1000/8      2583630 ns      2583700 ns          271 RowInvRate=2.5837us
BM_RemapTimestamps/10000/1000/8     3925305 ns      3925192 ns          178 RowInvRate=392.519ns
BM_RemapTimestamps/100000/1000/8   17493668 ns     17493263 ns           40 RowInvRate=174.933ns
*/

static void BM_RemapTimestamps(benchmark::State& state) {
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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT));
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

    TimestampValue timestamp;
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto timestamp_column =
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
            int unix_seconds = uniform_timestamp_increase(rng);
            timestamp.from_unix_second(static_cast<int64_t>(unix_seconds), 0);
            timestamp_column->append_datum(timestamp);
            calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
        }
        ctx->set_constant_columns({nullptr, unit_column, calendar_column, nullptr});
        state.ResumeTiming();
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_prepare(ctx.get(),
                                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::remap_timestamps_calendar_prepare(ctx.get(),
                                                                        FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar(ctx.get(),
                                                                    {timestamp_column, unit_column, calendar_column,
                                                                     calendar_id_column}).ok());
        ASSERT_OK(CelonisTimeFunctions::remap_timestamps_calendar_close(ctx.get(),
                                                                        FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::remap_timestamps_calendar_close(ctx.get(),
                                                                      FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
BENCHMARK(BM_RemapTimestamps)->ArgsProduct({{1000, 10000, 100000}, {10, 100, 1000}, {2, 4, 8}});

} // namespace starrocks

BENCHMARK_MAIN();
