#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/celonis/adjust_daily_timestamps.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "util/date_func.h"

/*
2025-07-13T14:25:14+00:00
Running ./be/build_Release/src/bench/celonis/output/adjust_daily_timestamps_bench
Run on (32 X 3242.99 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.04, 4.16, 3.13
// Number of cases / Average number of activities in a case
-----------------------------------------------------------------------------------------------------------
Benchmark                                                 Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------
BM_AdjustDailyTimestamps_WithSorting/1000/10         373971 ns       373925 ns         1859 CaseInvRate=373.925ns
BM_AdjustDailyTimestamps_WithSorting/10000/10       3948726 ns      3948519 ns          176 CaseInvRate=394.852ns
BM_AdjustDailyTimestamps_WithSorting/1000/20         612678 ns       612553 ns         1157 CaseInvRate=612.553ns
BM_AdjustDailyTimestamps_WithSorting/10000/20       6273506 ns      6273291 ns          111 CaseInvRate=627.329ns
BM_AdjustDailyTimestamps_WithSorting/1000/40        1101385 ns      1101262 ns          630 CaseInvRate=1.10126us
BM_AdjustDailyTimestamps_WithSorting/10000/40      11248907 ns     11248559 ns           61 CaseInvRate=1.12486us
BM_AdjustDailyTimestamps_WithoutSorting/1000/10       21813 ns        21727 ns        30267 CaseInvRate=21.7268ns
BM_AdjustDailyTimestamps_WithoutSorting/10000/10     287884 ns       287798 ns         2238 CaseInvRate=28.7798ns
BM_AdjustDailyTimestamps_WithoutSorting/1000/20       28936 ns        28882 ns        24011 CaseInvRate=28.8818ns
BM_AdjustDailyTimestamps_WithoutSorting/10000/20     446094 ns       446059 ns         1626 CaseInvRate=44.6059ns
BM_AdjustDailyTimestamps_WithoutSorting/1000/40       52757 ns        52690 ns        13355 CaseInvRate=52.6903ns
BM_AdjustDailyTimestamps_WithoutSorting/10000/40     618891 ns       618734 ns         1207 CaseInvRate=61.8734ns
*/

namespace starrocks {

TypeDescriptor TYPE_ARRAY_BIGINT = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT));
TypeDescriptor TYPE_ARRAY_BOOLEAN = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BOOLEAN));
TypeDescriptor TYPE_ARRAY_DATETIME = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME));

TimestampValue generate_random_timestamp(std::mt19937_64& rng, bool day_based = false) {
    // Define distribution range (Unix timestamps in milliseconds)
    // This range covers from 1970 to ~2100
    std::uniform_int_distribution<int64_t> dist(0, 4102444800000);
    int64_t millis = dist(rng);
    TimestampValue timestamp;
    timestamp.from_unix_second(millis / 1000, millis % 1000 * 1000);

    if (day_based) {
        // For day-based timestamps, truncate to day (zero out hours, minutes, seconds)
        timestamp.trunc_to_day();
    }

    return timestamp;
}

ColumnPtr generate_timestamp_arrays(std::mt19937_64& rng, int num_cases, int avg_activities_per_case) {
    ColumnPtr array_column = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_DATETIME), true);
    std::uniform_int_distribution<int> case_size_dist(1, avg_activities_per_case * 2); // Vary case sizes

    for (int i = 0; i < num_cases; i++) {
        DatumArray case_timestamps;
        int case_size = case_size_dist(rng);

        for (int j = 0; j < case_size; j++) {
            case_timestamps.push_back(Datum(generate_random_timestamp(rng)));
        }
        array_column->append_datum(Datum(case_timestamps));
    }
    return array_column;
}

ColumnPtr generate_is_day_based_arrays(std::mt19937_64& rng, const ColumnPtr& timestamp_column) {
    ColumnPtr array_column = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BOOLEAN), true);
    std::uniform_real_distribution<double> day_based_prob(0.0, 1.0);

    for (int i = 0; i < timestamp_column->size(); i++) {
        auto timestamp_array = timestamp_column->get(i).get_array();
        DatumArray is_day_based_array;

        for (const auto& timestamp_datum : timestamp_array) {
            // 30% chance of being day-based
            bool is_day_based = day_based_prob(rng) < 0.3;
            is_day_based_array.push_back(Datum(is_day_based));
        }
        array_column->append_datum(Datum(is_day_based_array));
    }
    return array_column;
}

ColumnPtr generate_sorting_arrays(std::mt19937_64& rng, const ColumnPtr& timestamp_column) {
    ColumnPtr array_column = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BIGINT), true);
    std::uniform_int_distribution<int64_t> sorting_dist(1, 1000);

    for (int i = 0; i < timestamp_column->size(); i++) {
        auto timestamp_array = timestamp_column->get(i).get_array();
        DatumArray sorting_array;

        for (const auto& timestamp_datum : timestamp_array) {
            sorting_array.push_back(Datum(sorting_dist(rng)));
        }
        array_column->append_datum(Datum(sorting_array));
    }
    return array_column;
}

static void BM_AdjustDailyTimestamps_WithSorting(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue
    int num_cases = state.range(0);
    int avg_activities_per_case = state.range(1); // Average activities per case

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                                                        TypeDescriptor::from_logical_type(TYPE_ARRAY),
                                                        TypeDescriptor::from_logical_type(TYPE_ARRAY)};

    FunctionContext::TypeDesc return_type;
    return_type.type = TYPE_STRUCT;
    return_type.children = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                            TypeDescriptor::from_logical_type(TYPE_ARRAY)};
    return_type.field_names = {"adjusted_timestamps", "reordering"};

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_cases = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_cases += num_cases;

        auto seed = 42;
        std::mt19937_64 rng(seed);

        // Generate test data
        ColumnPtr timestamp_column = generate_timestamp_arrays(rng, num_cases, avg_activities_per_case);
        ColumnPtr is_day_based_column = generate_is_day_based_arrays(rng, timestamp_column);
        ColumnPtr sorting_column = generate_sorting_arrays(rng, timestamp_column);

        state.ResumeTiming();

        auto result = CelonisAdjustDailyTimestamps::celonis_adjust_daily_timestamps(
                ctx.get(), {timestamp_column, is_day_based_column, sorting_column});
        EXPECT_TRUE(result.ok());
    }
    state.counters["CaseInvRate"] =
            benchmark::Counter(total_cases, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_AdjustDailyTimestamps_WithoutSorting(benchmark::State& state) {
    date::init_date_cache(); // This is needed for using TimestampValue
    int num_cases = state.range(0);
    int avg_activities_per_case = state.range(1); // Average activities per case

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                                                        TypeDescriptor::from_logical_type(TYPE_ARRAY)};

    FunctionContext::TypeDesc return_type;
    return_type.type = TYPE_STRUCT;
    return_type.children = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                            TypeDescriptor::from_logical_type(TYPE_ARRAY)};
    return_type.field_names = {"adjusted_timestamps", "reordering"};

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_cases = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_cases += num_cases;

        auto seed = 42;
        std::mt19937_64 rng(seed);

        // Generate test data
        ColumnPtr timestamp_column = generate_timestamp_arrays(rng, num_cases, avg_activities_per_case);
        ColumnPtr is_day_based_column = generate_is_day_based_arrays(rng, timestamp_column);

        state.ResumeTiming();

        auto result = CelonisAdjustDailyTimestamps::celonis_adjust_daily_timestamps(
                ctx.get(), {timestamp_column, is_day_based_column});
        EXPECT_TRUE(result.ok());
    }
    state.counters["CaseInvRate"] =
            benchmark::Counter(total_cases, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of cases / Average number of activities in a case
BENCHMARK(BM_AdjustDailyTimestamps_WithSorting)->ArgsProduct({{1000, 10000}, {10, 20, 40}});
BENCHMARK(BM_AdjustDailyTimestamps_WithoutSorting)->ArgsProduct({{1000, 10000}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();