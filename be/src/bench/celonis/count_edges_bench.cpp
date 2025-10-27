#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/table_function/count_edges.h"
#include "exprs/table_function/table_function.h"
#include "runtime/types.h"
#include "testutil/assert.h"
#include "types/logical_type.h"

namespace starrocks {

/*
2025-03-31T11:21:17+00:00
Running ./be/build_Release/src/bench/celonis/output/count_edges_bench
Run on (32 X 3241.62 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.29, 3.87, 2.32
// Args: Number of rows / Array size
-----------------------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------
BM_CountEdges/1000/8         176027 ns       176042 ns         4017 RowInvRate=176.042ns
BM_CountEdges/10000/8       1750942 ns      1750989 ns          399 RowInvRate=175.099ns
BM_CountEdges/100000/8     18584955 ns     18585745 ns           38 RowInvRate=185.857ns
BM_CountEdges/1000/16        383664 ns       383655 ns         1820 RowInvRate=383.655ns
BM_CountEdges/10000/16      3817318 ns      3817285 ns          183 RowInvRate=381.729ns
BM_CountEdges/100000/16    44198442 ns     44197711 ns           15 RowInvRate=441.977ns
BM_CountEdges/1000/32        731466 ns       731450 ns          962 RowInvRate=731.45ns
BM_CountEdges/10000/32      7513203 ns      7513276 ns           96 RowInvRate=751.328ns
BM_CountEdges/100000/32    89948854 ns     89947973 ns            8 RowInvRate=899.48ns
BM_CountEdges/1000/64       1405339 ns      1405170 ns          498 RowInvRate=1.40517us
BM_CountEdges/10000/64     16239819 ns     16240647 ns           42 RowInvRate=1.62406us
BM_CountEdges/100000/64   178924946 ns    178914710 ns            4 RowInvRate=1.78915us
BM_CountEdges/1000/128      3200492 ns      3200404 ns          220 RowInvRate=3.2004us
BM_CountEdges/10000/128    39629358 ns     39625880 ns           17 RowInvRate=3.96259us
BM_CountEdges/100000/128  405377698 ns    405370227 ns            2 RowInvRate=4.0537us
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
            result.emplace_back((Slice)activities[uniform_int(rng) % activities.size()]);
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

} // namespace starrocks

BENCHMARK_MAIN();
