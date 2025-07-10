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
2025-07-09T14:46:39+00:00
Running ./be/build_Release/src/bench/celonis/output/in_calendar_bench
Run on (32 X 2943.08 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.55, 8.08, 5.32
// Args: Number of rows / Number of calendar entries per id / Number of calendar ids
--------------------------------------------------------------------------------------
Benchmark                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------
BM_InCalendar/1000/10/2          81328 ns        81363 ns         8558 RowInvRate=81.3631ns
BM_InCalendar/10000/10/2        708965 ns       708989 ns          984 RowInvRate=70.8989ns
BM_InCalendar/100000/10/2      6954011 ns      6954044 ns          100 RowInvRate=69.5404ns
BM_InCalendar/1000/100/2        133634 ns       133709 ns         5234 RowInvRate=133.709ns
BM_InCalendar/10000/100/2       766601 ns       766615 ns          915 RowInvRate=76.6615ns
BM_InCalendar/100000/100/2     7040569 ns      7040395 ns           98 RowInvRate=70.4039ns
BM_InCalendar/1000/1000/2       697408 ns       697567 ns         1003 RowInvRate=697.567ns
BM_InCalendar/10000/1000/2     1350845 ns      1350918 ns          521 RowInvRate=135.092ns
BM_InCalendar/100000/1000/2    7649122 ns      7649045 ns           92 RowInvRate=76.4905ns
BM_InCalendar/1000/10/4          84597 ns        84641 ns         8258 RowInvRate=84.641ns
BM_InCalendar/10000/10/4        647185 ns       647207 ns         1078 RowInvRate=64.7207ns
BM_InCalendar/100000/10/4      6225772 ns      6225678 ns          111 RowInvRate=62.2568ns
BM_InCalendar/1000/100/4        188173 ns       188285 ns         3665 RowInvRate=188.285ns
BM_InCalendar/10000/100/4       766713 ns       766693 ns          913 RowInvRate=76.6693ns
BM_InCalendar/100000/100/4     6426668 ns      6426435 ns          108 RowInvRate=64.2643ns
BM_InCalendar/1000/1000/4      1379122 ns      1379256 ns          506 RowInvRate=1.37926us
BM_InCalendar/10000/1000/4     1964955 ns      1964904 ns          355 RowInvRate=196.49ns
BM_InCalendar/100000/1000/4    7670482 ns      7670523 ns           90 RowInvRate=76.7052ns
BM_InCalendar/1000/10/8         102316 ns       102365 ns         6769 RowInvRate=102.365ns
BM_InCalendar/10000/10/8        703565 ns       703588 ns          997 RowInvRate=70.3588ns
BM_InCalendar/100000/10/8      6656882 ns      6656786 ns          105 RowInvRate=66.5679ns
BM_InCalendar/1000/100/8        322489 ns       322609 ns         2137 RowInvRate=322.609ns
BM_InCalendar/10000/100/8       933290 ns       933335 ns          747 RowInvRate=93.3335ns
BM_InCalendar/100000/100/8     6969412 ns      6969216 ns          101 RowInvRate=69.6922ns
BM_InCalendar/1000/1000/8      2729408 ns      2729564 ns          257 RowInvRate=2.72956us
BM_InCalendar/10000/1000/8     3363714 ns      3363812 ns          208 RowInvRate=336.381ns
BM_InCalendar/100000/1000/8    9583175 ns      9583115 ns           74 RowInvRate=95.8312ns
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
    std::optional<std::string> serialized_calendar = to_base64_encoded_string(calendar_proto, 1LL << 30, true);
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
