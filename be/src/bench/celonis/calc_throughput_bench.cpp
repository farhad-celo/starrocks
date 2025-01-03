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
2024-12-30T02:04:06+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_throughput_bench
Run on (32 X 3242.42 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.52, 2.19, 1.13
// Args: Number of rows / Event array size / Max string length (for string type)
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_CalcThroughputInt/1000/10          20049 ns        19940 ns        35507 RowInvRate=19.9404ns
BM_CalcThroughputInt/10000/10        167702 ns       167659 ns         4143 RowInvRate=16.7659ns
BM_CalcThroughputInt/1000/20          23754 ns        23658 ns        29655 RowInvRate=23.6576ns
BM_CalcThroughputInt/10000/20        210758 ns       210707 ns         3292 RowInvRate=21.0707ns
BM_CalcThroughputStr/1000/10/8        34998 ns        34897 ns        20206 RowInvRate=34.8971ns
BM_CalcThroughputStr/10000/10/8      305132 ns       305078 ns         2295 RowInvRate=30.5078ns
BM_CalcThroughputStr/1000/20/8        46804 ns        46706 ns        14997 RowInvRate=46.706ns
BM_CalcThroughputStr/10000/20/8      429541 ns       429520 ns         1647 RowInvRate=42.952ns
BM_CalcThroughputStr/1000/10/16       34823 ns        34730 ns        19896 RowInvRate=34.7298ns
BM_CalcThroughputStr/10000/10/16     301108 ns       301085 ns         2346 RowInvRate=30.1085ns
BM_CalcThroughputStr/1000/20/16       46126 ns        46030 ns        15321 RowInvRate=46.03ns
BM_CalcThroughputStr/10000/20/16     410406 ns       410354 ns         1692 RowInvRate=41.0354ns
*/

TypeDescriptor array_type(const LogicalType& element_type) {
    starrocks::TypeDescriptor t;
    t.type = starrocks::TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = element_type;
    t.children[0].len = (element_type == starrocks::TYPE_VARCHAR || element_type == starrocks::TYPE_CHAR) ? 10 : -1;
    return t;
}

static void do_bench(benchmark::State& state, LogicalType activity_type) {
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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(activity_type)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(activity_type)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        auto activity_array_col = ColumnHelper::create_column(array_type(activity_type), false);
        auto timestamp_array_col = ColumnHelper::create_column(array_type(TYPE_BIGINT), false);
        auto start_activity_col = ColumnHelper::create_column(TypeDescriptor(activity_type), false);
        auto end_activity_col = ColumnHelper::create_column(TypeDescriptor(activity_type), false);
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

        if (activity_type == TYPE_VARCHAR) {
            int max_str_length = state.range(2);
            assert(max_str_length < alphanum.size());
            std::generate(activities.begin(), activities.end(), [&]() { return gen_rand_str(max_str_length); });
        } else if (activity_type == TYPE_BIGINT) {
            std::generate(activities.begin(), activities.end(), [&]() { return uniform_int(rng); });
        } else {
            std::cerr << "activity type not supported" << std::endl;
        }
        for (int i = 0; i < num_rows; i++) {
            activity_array_col->append_datum(activities);
            timestamp_array_col->append_datum(timestamps);
            start_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);
            end_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);
            start_label_col->append_datum(Slice("first"));
            end_label_col->append_datum(Slice("first"));
        }
        state.ResumeTiming();
        EXPECT_TRUE(CelonisCalcThroughputFunctions::celonis_calc_throughput(ctx.get(), {activity_array_col, timestamp_array_col,
                                                                                        start_activity_col,
                                                                                        end_activity_col,
                                                                                        start_label_col,
                                                                                        end_label_col}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}
static void BM_CalcThroughputInt(benchmark::State& state) {
    do_bench(state, TYPE_BIGINT);
}

static void BM_CalcThroughputStr(benchmark::State& state) {
    do_bench(state, TYPE_VARCHAR);
}

// Args: Number of rows / Event array size
BENCHMARK(BM_CalcThroughputInt)->ArgsProduct({{1000, 10000}, {10, 20}});

// Args: Number of rows / Event array size / Max string length (for string type)
BENCHMARK(BM_CalcThroughputStr)->ArgsProduct({{1000, 10000}, {10, 20}, {8, 16}});

}  // namespace starrocks

BENCHMARK_MAIN();
