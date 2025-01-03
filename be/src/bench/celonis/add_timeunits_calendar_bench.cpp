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
2024-12-28T21:03:51+00:00
Running ./be/build_Release/src/bench/celonis/output/add_timeunits_calendar_bench
Run on (32 X 3279.94 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.35, 12.20, 20.35
// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
------------------------------------------------------------------------------------------------
Benchmark                                      Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------------------
BM_AddTimeunitsCalendar/1000/10/2         128799 ns       128812 ns         5444 RowInvRate=128.812ns
BM_AddTimeunitsCalendar/10000/10/2       1197737 ns      1197649 ns          586 RowInvRate=119.765ns
BM_AddTimeunitsCalendar/100000/10/2     11800890 ns     11800406 ns           59 RowInvRate=118.004ns
BM_AddTimeunitsCalendar/1000/100/2        177301 ns       177324 ns         3971 RowInvRate=177.324ns
BM_AddTimeunitsCalendar/10000/100/2      1251869 ns      1251810 ns          561 RowInvRate=125.181ns
BM_AddTimeunitsCalendar/100000/100/2    11904086 ns     11904158 ns           59 RowInvRate=119.042ns
BM_AddTimeunitsCalendar/1000/1000/2       653916 ns       654021 ns         1062 RowInvRate=654.021ns
BM_AddTimeunitsCalendar/10000/1000/2     1746528 ns      1746499 ns          400 RowInvRate=174.65ns
BM_AddTimeunitsCalendar/100000/1000/2   12480814 ns     12480828 ns           55 RowInvRate=124.808ns
BM_AddTimeunitsCalendar/1000/10/4         137439 ns       137447 ns         5126 RowInvRate=137.447ns
BM_AddTimeunitsCalendar/10000/10/4       1203699 ns      1203556 ns          580 RowInvRate=120.356ns
BM_AddTimeunitsCalendar/100000/10/4     11906208 ns     11905938 ns           59 RowInvRate=119.059ns
BM_AddTimeunitsCalendar/1000/100/4        236815 ns       236852 ns         2966 RowInvRate=236.852ns
BM_AddTimeunitsCalendar/10000/100/4      1311867 ns      1311797 ns          532 RowInvRate=131.18ns
BM_AddTimeunitsCalendar/100000/100/4    11993566 ns     11993652 ns           57 RowInvRate=119.937ns
BM_AddTimeunitsCalendar/1000/1000/4      1289326 ns      1289340 ns          540 RowInvRate=1.28934us
BM_AddTimeunitsCalendar/10000/1000/4     2376835 ns      2376840 ns          295 RowInvRate=237.684ns
BM_AddTimeunitsCalendar/100000/1000/4   13114422 ns     13113629 ns           53 RowInvRate=131.136ns
BM_AddTimeunitsCalendar/1000/10/8         154368 ns       154379 ns         4563 RowInvRate=154.379ns
BM_AddTimeunitsCalendar/10000/10/8       1220117 ns      1220112 ns          575 RowInvRate=122.011ns
BM_AddTimeunitsCalendar/100000/10/8     11885649 ns     11885569 ns           59 RowInvRate=118.856ns
BM_AddTimeunitsCalendar/1000/100/8        353518 ns       353557 ns         1986 RowInvRate=353.557ns
BM_AddTimeunitsCalendar/10000/100/8      1429121 ns      1429105 ns          489 RowInvRate=142.911ns
BM_AddTimeunitsCalendar/100000/100/8    12186061 ns     12185729 ns           56 RowInvRate=121.857ns
BM_AddTimeunitsCalendar/1000/1000/8      2484079 ns      2484019 ns          282 RowInvRate=2.48402us
BM_AddTimeunitsCalendar/10000/1000/8     3594323 ns      3594191 ns          194 RowInvRate=359.419ns
BM_AddTimeunitsCalendar/100000/1000/8   14711865 ns     14711028 ns           48 RowInvRate=147.11ns
*/

static void BM_AddTimeunitsCalendar(benchmark::State& state) {
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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DATETIME));
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
        auto add_column =
            ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
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
            timestamp.from_unix_second(unix_seconds);
            timestamp_column->append_datum(timestamp);
            int add = uniform_timestamp_increase(rng);
            add_column->append_datum(static_cast<int64_t>(add));
            calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
        }
        ctx->set_constant_columns({nullptr, nullptr, unit_column, calendar_column, nullptr});
        state.ResumeTiming();
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_prepare(ctx.get(),
                                                                       FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::add_timeunits_calendar_prepare(ctx.get(),
                                                                     FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::add_timeunits_calendar(ctx.get(),
                                                                 {timestamp_column, add_column, unit_column,
                                                                  calendar_column,
                                                                  calendar_id_column}).ok());
        ASSERT_OK(CelonisTimeFunctions::add_timeunits_calendar_close(ctx.get(),
                                                                     FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::add_timeunits_calendar_close(ctx.get(),
                                                                   FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
BENCHMARK(BM_AddTimeunitsCalendar)->ArgsProduct({{1000, 10000, 100000}, {10, 100, 1000}, {2, 4, 8}});

} // namespace starrocks

BENCHMARK_MAIN();
