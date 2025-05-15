#include <benchmark/benchmark.h>
#include <cassert>
#include <gtest/gtest.h>
#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/calc_throughput.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "types/logical_type.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-05-14T21:10:46+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_throughput_bench
Run on (32 X 3086.16 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 14.83, 6.99, 4.24
// Args: Number of rows / Event array size / Max string length (for string type)
-------------------------------------------------------------------------------------------------------
Benchmark                                             Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------
BM_CalcThroughputIntFirstLast/1000/10             19301 ns        19208 ns        36267 RowInvRate=19.208ns
BM_CalcThroughputIntFirstLast/10000/10           159241 ns       159241 ns         4358 RowInvRate=15.9241ns
BM_CalcThroughputIntFirstLast/1000/20             26365 ns        26285 ns        26918 RowInvRate=26.2852ns
BM_CalcThroughputIntFirstLast/10000/20           229296 ns       229260 ns         3041 RowInvRate=22.926ns

BM_CalcThroughputStrFirstLast/1000/10/8           55200 ns        55181 ns        12693 RowInvRate=55.1805ns
BM_CalcThroughputStrFirstLast/10000/10/8         515450 ns       515478 ns         1372 RowInvRate=51.5478ns
BM_CalcThroughputStrFirstLast/1000/20/8           90077 ns        90076 ns         7792 RowInvRate=90.0764ns
BM_CalcThroughputStrFirstLast/10000/20/8         859092 ns       859041 ns          833 RowInvRate=85.9041ns
BM_CalcThroughputStrFirstLast/1000/10/16          55180 ns        55159 ns        12681 RowInvRate=55.1588ns
BM_CalcThroughputStrFirstLast/10000/10/16        501412 ns       501463 ns         1000 RowInvRate=50.1463ns
BM_CalcThroughputStrFirstLast/1000/20/16         168656 ns       115241 ns         7963 RowInvRate=115.241ns
BM_CalcThroughputStrFirstLast/10000/20/16       2137988 ns      1408076 ns          644 RowInvRate=140.808ns

BM_CalcThroughputStrCaseStartEnd/1000/10/8        60853 ns        58032 ns        11107 RowInvRate=58.0322ns
BM_CalcThroughputStrCaseStartEnd/10000/10/8      469142 ns       469032 ns         1418 RowInvRate=46.9032ns
BM_CalcThroughputStrCaseStartEnd/1000/20/8        76874 ns        76836 ns         9206 RowInvRate=76.8359ns
BM_CalcThroughputStrCaseStartEnd/10000/20/8      807143 ns       806980 ns          983 RowInvRate=80.698ns
BM_CalcThroughputStrCaseStartEnd/1000/10/16       63755 ns        52966 ns        14635 RowInvRate=52.9659ns
BM_CalcThroughputStrCaseStartEnd/10000/10/16     660623 ns       530047 ns         1527 RowInvRate=53.0047ns
BM_CalcThroughputStrCaseStartEnd/1000/20/16       73858 ns        73824 ns         7355 RowInvRate=73.8243ns
BM_CalcThroughputStrCaseStartEnd/10000/20/16     716145 ns       715865 ns         1026 RowInvRate=71.5865ns
*/

TypeDescriptor array_type(const LogicalType& element_type) {
    starrocks::TypeDescriptor t;
    t.type = starrocks::TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = element_type;
    t.children[0].len = (element_type == starrocks::TYPE_VARCHAR || element_type == starrocks::TYPE_CHAR) ? 10 : -1;
    return t;
}


template <LogicalType ActivityLT>
static void do_bench(benchmark::State& state, const std::string& start_label, const std::string& end_label) {
    int num_rows = state.range(0);
    int event_array_size = state.range(1);
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_int;
    uniform_int.param(UniformInt::param_type(1, 100000));

    static std::string alphanum =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    auto gen_rand_str = [&](int max_length) {
        int str_len = 1 + uniform_int(rng) % max_length;
        int str_start = std::min(uniform_int(rng) % alphanum.size(), alphanum.size() - str_len);
        return Slice(alphanum.c_str() + str_start, str_len);
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(ActivityLT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(ActivityLT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        auto activity_array_col = ColumnHelper::create_column(array_type(ActivityLT), false);
        auto timestamp_array_col = ColumnHelper::create_column(array_type(TYPE_BIGINT), false);
        auto start_activity_col = ColumnHelper::create_column(TypeDescriptor(ActivityLT), false);
        auto end_activity_col = ColumnHelper::create_column(TypeDescriptor(ActivityLT), false);
        auto start_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        DatumArray timestamps;
        timestamps.resize(event_array_size);
        std::generate(timestamps.begin(), timestamps.end(), [&]() { return uniform_int(rng); });
        auto timestamp_cmp = [](const Datum& d1, const Datum& d2) {
            return d1.get_int64() < d2.get_int64();
        };
        std::sort(timestamps.begin(), timestamps.end(), timestamp_cmp);

        DatumArray activities;
        activities.resize(event_array_size);

        if (ActivityLT == TYPE_VARCHAR) {
            int max_str_length = state.range(2);
            assert(max_str_length < alphanum.size());
            std::generate(activities.begin(), activities.end(), [&]() { return gen_rand_str(max_str_length); });
        } else if (ActivityLT == TYPE_BIGINT) {
            std::generate(activities.begin(), activities.end(), [&]() { return uniform_int(rng); });
        } else {
            std::cerr << "activity type not supported" << std::endl;
        }
        for (int i = 0; i < num_rows; i++) {
            activity_array_col->append_datum(activities);
            timestamp_array_col->append_datum(timestamps);
            start_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);
            end_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);
            start_label_col->append_datum(Slice(start_label));
            end_label_col->append_datum(Slice(end_label));
        }
        state.ResumeTiming();
        EXPECT_TRUE(CelonisCalcThroughputFunctions<ActivityLT>::celonis_calc_throughput(ctx.get(), {activity_array_col,
                                                                                                    timestamp_array_col,
                                                                                                    start_activity_col,
                                                                                                    end_activity_col,
                                                                                                    start_label_col,
                                                                                                    end_label_col}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}
static void BM_CalcThroughputIntFirstLast(benchmark::State& state) {
    do_bench<TYPE_BIGINT>(state, "first", "last");
}

static void BM_CalcThroughputStrFirstLast(benchmark::State& state) {
    do_bench<TYPE_VARCHAR>(state, "first", "last");
}

static void BM_CalcThroughputStrCaseStartEnd(benchmark::State& state) {
    do_bench<TYPE_VARCHAR>(state, "case_start", "case_end");
}

// Args: Number of rows / Event array size
BENCHMARK(BM_CalcThroughputIntFirstLast)->ArgsProduct({{1000, 10000}, {10, 20}});

// Args: Number of rows / Event array size / Max string length (for string type)
BENCHMARK(BM_CalcThroughputStrFirstLast)->ArgsProduct({{1000, 10000}, {10, 20}, {8, 16}});

// Args: Number of rows / Event array size / Max string length (for string type)
BENCHMARK(BM_CalcThroughputStrCaseStartEnd)->ArgsProduct({{1000, 10000}, {10, 20}, {8, 16}});

}  // namespace starrocks

BENCHMARK_MAIN();
