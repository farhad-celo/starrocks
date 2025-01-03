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
2024-12-28T21:00:15+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_timestamps_calendar_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.74, 22.45, 25.06
// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_RemapTimestamps/1000/10/2         197084 ns       197106 ns         3528 RowInvRate=197.106ns
BM_RemapTimestamps/10000/10/2       1869947 ns      1869910 ns          373 RowInvRate=186.991ns
BM_RemapTimestamps/100000/10/2     18503397 ns     18503041 ns           38 RowInvRate=185.03ns
BM_RemapTimestamps/1000/100/2        274641 ns       274662 ns         2580 RowInvRate=274.662ns
BM_RemapTimestamps/10000/100/2      2016226 ns      2016148 ns          346 RowInvRate=201.615ns
BM_RemapTimestamps/100000/100/2    19448982 ns     19448672 ns           36 RowInvRate=194.487ns
BM_RemapTimestamps/1000/1000/2       808239 ns       808291 ns          871 RowInvRate=808.291ns
BM_RemapTimestamps/10000/1000/2     2657844 ns      2657782 ns          263 RowInvRate=265.778ns
BM_RemapTimestamps/100000/1000/2   20504491 ns     20504501 ns           34 RowInvRate=205.045ns
BM_RemapTimestamps/1000/10/4         209673 ns       209675 ns         3298 RowInvRate=209.675ns
BM_RemapTimestamps/10000/10/4       1920626 ns      1920484 ns          365 RowInvRate=192.048ns
BM_RemapTimestamps/100000/10/4     19124124 ns     19123991 ns           36 RowInvRate=191.24ns
BM_RemapTimestamps/1000/100/4        347832 ns       347859 ns         1986 RowInvRate=347.859ns
BM_RemapTimestamps/10000/100/4      2141896 ns      2141827 ns          326 RowInvRate=214.183ns
BM_RemapTimestamps/100000/100/4    19736223 ns     19735368 ns           35 RowInvRate=197.354ns
BM_RemapTimestamps/1000/1000/4      1392583 ns      1392643 ns          503 RowInvRate=1.39264us
BM_RemapTimestamps/10000/1000/4     3240929 ns      3240873 ns          212 RowInvRate=324.087ns
BM_RemapTimestamps/100000/1000/4   21767671 ns     21767397 ns           32 RowInvRate=217.674ns
BM_RemapTimestamps/1000/10/8         235746 ns       235755 ns         2989 RowInvRate=235.755ns
BM_RemapTimestamps/10000/10/8       1987000 ns      1986946 ns          349 RowInvRate=198.695ns
BM_RemapTimestamps/100000/10/8     19369472 ns     19368995 ns           36 RowInvRate=193.69ns
BM_RemapTimestamps/1000/100/8        508614 ns       508676 ns         1000 RowInvRate=508.676ns
BM_RemapTimestamps/10000/100/8      2372580 ns      2372379 ns          295 RowInvRate=237.238ns
BM_RemapTimestamps/100000/100/8    20786390 ns     20785883 ns           33 RowInvRate=207.859ns
BM_RemapTimestamps/1000/1000/8      2597803 ns      2597734 ns          268 RowInvRate=2.59773us
BM_RemapTimestamps/10000/1000/8     4553053 ns      4552719 ns          153 RowInvRate=455.272ns
BM_RemapTimestamps/100000/1000/8   23944393 ns     23944193 ns           29 RowInvRate=239.442ns
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
