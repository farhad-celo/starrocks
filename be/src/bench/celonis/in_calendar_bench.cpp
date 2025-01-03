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
2024-12-28T21:02:16+00:00
Running ./be/build_Release/src/bench/celonis/output/in_calendar_bench
Run on (32 X 3071.23 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.34, 15.45, 22.17
// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
--------------------------------------------------------------------------------------
Benchmark                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------
BM_InCalendar/1000/10/2          75018 ns        75030 ns         9263 RowInvRate=75.0302ns
BM_InCalendar/10000/10/2        625541 ns       625541 ns         1118 RowInvRate=62.5541ns
BM_InCalendar/100000/10/2      6196828 ns      6196339 ns          115 RowInvRate=61.9634ns
BM_InCalendar/1000/100/2        133604 ns       133628 ns         5191 RowInvRate=133.628ns
BM_InCalendar/10000/100/2       694420 ns       694325 ns         1009 RowInvRate=69.4325ns
BM_InCalendar/100000/100/2     6206157 ns      6205606 ns          113 RowInvRate=62.0561ns
BM_InCalendar/1000/1000/2       688884 ns       688970 ns         1063 RowInvRate=688.97ns
BM_InCalendar/10000/1000/2     1291165 ns      1291102 ns          549 RowInvRate=129.11ns
BM_InCalendar/100000/1000/2    6795625 ns      6795445 ns          104 RowInvRate=67.9544ns
BM_InCalendar/1000/10/4          84143 ns        84143 ns         8512 RowInvRate=84.1433ns
BM_InCalendar/10000/10/4        650212 ns       650174 ns         1081 RowInvRate=65.0174ns
BM_InCalendar/100000/10/4      6258416 ns      6258306 ns          112 RowInvRate=62.5831ns
BM_InCalendar/1000/100/4        221161 ns       221207 ns         3244 RowInvRate=221.207ns
BM_InCalendar/10000/100/4       775528 ns       775500 ns          899 RowInvRate=77.55ns
BM_InCalendar/100000/100/4     6398059 ns      6398116 ns          110 RowInvRate=63.9812ns
BM_InCalendar/1000/1000/4      1276836 ns      1276921 ns          553 RowInvRate=1.27692us
BM_InCalendar/10000/1000/4     1851544 ns      1851554 ns          381 RowInvRate=185.155ns
BM_InCalendar/100000/1000/4    7584404 ns      7584356 ns           91 RowInvRate=75.8436ns
BM_InCalendar/1000/10/8         103644 ns       103655 ns         6873 RowInvRate=103.655ns
BM_InCalendar/10000/10/8        670706 ns       670719 ns         1035 RowInvRate=67.0719ns
BM_InCalendar/100000/10/8      6314319 ns      6314285 ns          110 RowInvRate=63.1428ns
BM_InCalendar/1000/100/8        389975 ns       390045 ns         1850 RowInvRate=390.045ns
BM_InCalendar/10000/100/8       923553 ns       923526 ns          774 RowInvRate=92.3526ns
BM_InCalendar/100000/100/8     6589608 ns      6589193 ns          105 RowInvRate=65.8919ns
BM_InCalendar/1000/1000/8      2518905 ns      2518845 ns          280 RowInvRate=2.51884us
BM_InCalendar/10000/1000/8     3095205 ns      3094994 ns          227 RowInvRate=309.499ns
BM_InCalendar/100000/1000/8    9034993 ns      9035010 ns           79 RowInvRate=90.3501ns
*/

static void BM_InCalendar(benchmark::State& state) {
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
        auto calendar_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto calendar_id_column =
                ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        calendar_column->append_datum(calendar_array);
        calendar_column = ConstColumn::create(calendar_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            int unix_seconds = uniform_timestamp_increase(rng);
            timestamp.from_unix_second(unix_seconds);
            timestamp_column->append_datum(timestamp);
            calendar_id_column->append_datum(Slice(calendar_ids[uniform_calendar_id(rng)]));
        }
        ctx->set_constant_columns({nullptr, calendar_column, nullptr});
        state.ResumeTiming();
        ASSERT_OK(CelonisTimeFunctions::in_calendar_prepare(ctx.get(),
                                                            FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::in_calendar_prepare(ctx.get(),
                                                          FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisTimeFunctions::in_calendar(ctx.get(),
                                                      {timestamp_column, calendar_column, calendar_id_column}).ok());
        ASSERT_OK(CelonisTimeFunctions::in_calendar_close(ctx.get(),
                                                          FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(
                CelonisTimeFunctions::in_calendar_close(ctx.get(),
                                                        FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
BENCHMARK(BM_InCalendar)->ArgsProduct({{1000, 10000, 100000}, {10, 100, 1000}, {2, 4, 8}});

} // namespace starrocks

BENCHMARK_MAIN();
