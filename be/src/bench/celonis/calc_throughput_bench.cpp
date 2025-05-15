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
2025-05-15T16:38:45+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_throughput_bench
Run on (32 X 3275.23 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.43, 4.48, 1.91
// Args: Number of rows / Event array size / Max string length (for string type)
-------------------------------------------------------------------------------------------------------
Benchmark                                             Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------
BM_CalcThroughputIntFirstLast/1000/10             21236 ns        21148 ns        33165 RowInvRate=21.1483ns
BM_CalcThroughputIntFirstLast/10000/10           177411 ns       177402 ns         3937 RowInvRate=17.7402ns
BM_CalcThroughputIntFirstLast/1000/20             27185 ns        27094 ns        25869 RowInvRate=27.0942ns
BM_CalcThroughputIntFirstLast/10000/20           235215 ns       235217 ns         2977 RowInvRate=23.5217ns

BM_CalcThroughputStrFirstLast/1000/10/8           47057 ns        47041 ns        14881 RowInvRate=47.0409ns
BM_CalcThroughputStrFirstLast/10000/10/8         438360 ns       438366 ns         1588 RowInvRate=43.8366ns
BM_CalcThroughputStrFirstLast/1000/20/8           70981 ns        70970 ns         9925 RowInvRate=70.9696ns
BM_CalcThroughputStrFirstLast/10000/20/8         689114 ns       689130 ns         1028 RowInvRate=68.913ns
BM_CalcThroughputStrFirstLast/1000/10/16          46682 ns        46658 ns        14982 RowInvRate=46.658ns
BM_CalcThroughputStrFirstLast/10000/10/16        425522 ns       425556 ns         1628 RowInvRate=42.5556ns
BM_CalcThroughputStrFirstLast/1000/20/16          70247 ns        70241 ns        10011 RowInvRate=70.2408ns
BM_CalcThroughputStrFirstLast/10000/20/16        643841 ns       643870 ns         1092 RowInvRate=64.387ns

BM_CalcThroughputStrCaseStartEnd/1000/10/8        30874 ns        30835 ns        22616 RowInvRate=30.8349ns
BM_CalcThroughputStrCaseStartEnd/10000/10/8      282310 ns       282329 ns         2513 RowInvRate=28.2329ns
BM_CalcThroughputStrCaseStartEnd/1000/20/8        40867 ns        40855 ns        17108 RowInvRate=40.8547ns
BM_CalcThroughputStrCaseStartEnd/10000/20/8      440882 ns       440909 ns         1731 RowInvRate=44.0909ns
BM_CalcThroughputStrCaseStartEnd/1000/10/16       30986 ns        30950 ns        22557 RowInvRate=30.9499ns
BM_CalcThroughputStrCaseStartEnd/10000/10/16     277894 ns       277936 ns         2561 RowInvRate=27.7936ns
BM_CalcThroughputStrCaseStartEnd/1000/20/16       40824 ns        40806 ns        17065 RowInvRate=40.8061ns
BM_CalcThroughputStrCaseStartEnd/10000/20/16     382351 ns       382428 ns         1803 RowInvRate=38.2428ns
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
