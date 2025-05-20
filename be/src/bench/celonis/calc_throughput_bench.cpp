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
2025-05-20T18:03:12+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_throughput_bench
Run on (32 X 3243.02 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.12, 4.64, 3.31
// Args: Number of rows / Event array size / Max string length (for string type)
-------------------------------------------------------------------------------------------------------
Benchmark                                             Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------
BM_CalcThroughputIntFirstLast/1000/10             19315 ns        19212 ns        36131 RowInvRate=19.2121ns
BM_CalcThroughputIntFirstLast/10000/10           158241 ns       158229 ns         4390 RowInvRate=15.8229ns
BM_CalcThroughputIntFirstLast/1000/20             22926 ns        22837 ns        30899 RowInvRate=22.8368ns
BM_CalcThroughputIntFirstLast/10000/20           201588 ns       201560 ns         3508 RowInvRate=20.156ns

BM_CalcThroughputStrFirstLast/1000/10/8           44690 ns        44669 ns        15524 RowInvRate=44.6688ns
BM_CalcThroughputStrFirstLast/10000/10/8         399450 ns       399468 ns         1739 RowInvRate=39.9468ns
BM_CalcThroughputStrFirstLast/1000/20/8           66526 ns        66504 ns        10532 RowInvRate=66.504ns
BM_CalcThroughputStrFirstLast/10000/20/8         662755 ns       662687 ns         1161 RowInvRate=66.2687ns
BM_CalcThroughputStrFirstLast/1000/10/16          44619 ns        44588 ns        15769 RowInvRate=44.5878ns
BM_CalcThroughputStrFirstLast/10000/10/16        396306 ns       396336 ns         1775 RowInvRate=39.6336ns
BM_CalcThroughputStrFirstLast/1000/20/16          65240 ns        65223 ns        10911 RowInvRate=65.2232ns
BM_CalcThroughputStrFirstLast/10000/20/16        596452 ns       596404 ns         1171 RowInvRate=59.6404ns

BM_CalcThroughputStrCaseStartEnd/1000/10/8        29774 ns        29729 ns        23500 RowInvRate=29.7294ns
BM_CalcThroughputStrCaseStartEnd/10000/10/8      260236 ns       260270 ns         2684 RowInvRate=26.027ns
BM_CalcThroughputStrCaseStartEnd/1000/20/8        39850 ns        39816 ns        17527 RowInvRate=39.816ns
BM_CalcThroughputStrCaseStartEnd/10000/20/8      377836 ns       377849 ns         1843 RowInvRate=37.7849ns
BM_CalcThroughputStrCaseStartEnd/1000/10/16       29734 ns        29682 ns        23387 RowInvRate=29.6818ns
BM_CalcThroughputStrCaseStartEnd/10000/10/16     263567 ns       263589 ns         2712 RowInvRate=26.3589ns
BM_CalcThroughputStrCaseStartEnd/1000/20/16       39988 ns        39957 ns        17495 RowInvRate=39.9575ns
BM_CalcThroughputStrCaseStartEnd/10000/20/16     414787 ns       414747 ns         1810 RowInvRate=41.4747ns
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
