#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/table_function/table_function.h"
#include "exprs/celonis/table_function/count_edges.h"
#include "runtime/types.h"
#include "types/logical_type.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2024-12-29T23:04:59+00:00
Running ./be/build_Release/src/bench/celonis/output/count_edges_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.14, 2.73, 2.56
// Args: Number of rows / Array size
-----------------------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------
BM_CountEdges/1000/8         307945 ns       307955 ns         2280 RowInvRate=307.955ns
BM_CountEdges/10000/8       3077499 ns      3077423 ns          228 RowInvRate=307.742ns
BM_CountEdges/100000/8     31857417 ns     31855697 ns           22 RowInvRate=318.557ns
BM_CountEdges/1000/16        638017 ns       638018 ns         1107 RowInvRate=638.018ns
BM_CountEdges/10000/16      6314378 ns      6314202 ns          111 RowInvRate=631.42ns
BM_CountEdges/100000/16    68411239 ns     68410403 ns           10 RowInvRate=684.104ns
BM_CountEdges/1000/32       1202728 ns      1202659 ns          582 RowInvRate=1.20266us
BM_CountEdges/10000/32     12069685 ns     12069815 ns           57 RowInvRate=1.20698us
BM_CountEdges/100000/32   134411127 ns    134411011 ns            5 RowInvRate=1.34411us
BM_CountEdges/1000/64       2243068 ns      2243067 ns          312 RowInvRate=2.24307us
BM_CountEdges/10000/64     24209521 ns     24209846 ns           29 RowInvRate=2.42098us
BM_CountEdges/100000/64   262439599 ns    262432074 ns            3 RowInvRate=2.62432us
BM_CountEdges/1000/128      4410428 ns      4408980 ns          159 RowInvRate=4.40898us
BM_CountEdges/10000/128    53527745 ns     53527431 ns           13 RowInvRate=5.35274us
BM_CountEdges/100000/128  540525612 ns    540469774 ns            1 RowInvRate=5.4047us
*/

TypeDescriptor array_type(const LogicalType& element_type) {
    starrocks::TypeDescriptor t;
    t.type = starrocks::TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = element_type;
    t.children[0].len = (element_type == starrocks::TYPE_VARCHAR || element_type == starrocks::TYPE_CHAR) ? 30 : -1;
    return t;
}

static void do_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int array_size = state.range(1);
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_int;
    uniform_int.param(UniformInt::param_type(1, 250));

    std::vector<std::string> activities;
    for (int i = 0; i < array_size; ++i) {
        std::stringstream str;
        str << "ACTIVITY_NUMBER_" << i;
        activities.push_back(str.str());
    }
    auto gen_rand_vector = [&](int max_length) {
        int array_len = 1 + uniform_int(rng) % max_length;
        DatumArray result;
        for (int i = 0; i < array_len; ++i) {
            result.emplace_back((Slice) activities[uniform_int(rng) % activities.size()]);
        }
        return result;
    };
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr activity_column = ColumnHelper::create_column(array_type(TYPE_VARCHAR), false);
        for (int i = 0; i < num_rows; ++i) {
            activity_column->append_datum(gen_rand_vector(array_size));
        }
        TableFunctionState* table_state;
        auto function = std::make_unique<CountEdges>();
        Columns input;
        input.push_back(activity_column);
        ASSERT_OK(function->init({}, &table_state));
        table_state->set_params(input);
        ASSERT_OK(function->prepare(table_state));
        state.ResumeTiming();
        auto result = function->process(nullptr, table_state);
        ASSERT_OK(function->close(nullptr, table_state));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_CountEdges(benchmark::State& state) {
    do_bench(state);
}

BENCHMARK(BM_CountEdges)->ArgsProduct({{1000, 10000, 100000}, {8, 16, 32, 64, 128}});

}  // namespace starrocks

BENCHMARK_MAIN();
