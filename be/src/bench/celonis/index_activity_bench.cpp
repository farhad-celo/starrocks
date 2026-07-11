#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/index_activity.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-31T18:38:31+00:00
Running ./be/build_Release/src/bench/celonis/output/index_activity_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.31, 9.45, 9.03
// Number of rows / Variant length
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_IndexActivityOrderForward/10000/20     1501251 ns      1501140 ns          466 RowInvRate=150.114ns
BM_IndexActivityOrderForward/20000/20     2969024 ns      2968861 ns          234 RowInvRate=148.443ns
BM_IndexActivityOrderForward/10000/100    8559068 ns      8558160 ns           82 RowInvRate=855.816ns
BM_IndexActivityOrderForward/20000/100   16257135 ns     16256246 ns           43 RowInvRate=812.812ns
BM_IndexActivityOrderReverse/10000/20     1668418 ns      1667933 ns          420 RowInvRate=166.793ns
BM_IndexActivityOrderReverse/20000/20     3307822 ns      3307603 ns          211 RowInvRate=165.38ns
BM_IndexActivityOrderReverse/10000/100    9430258 ns      9429161 ns           77 RowInvRate=942.916ns
BM_IndexActivityOrderReverse/20000/100   18327746 ns     18323580 ns           39 RowInvRate=916.179ns
BM_IndexActivityLoopForward/10000/20      3103598 ns      3103186 ns          226 RowInvRate=310.319ns
BM_IndexActivityLoopForward/20000/20      6201577 ns      6200769 ns          113 RowInvRate=310.038ns
BM_IndexActivityLoopForward/10000/100    16816116 ns     16814726 ns           43 RowInvRate=1.68147us
BM_IndexActivityLoopForward/20000/100    36319116 ns     36318055 ns           19 RowInvRate=1.8159us
BM_IndexActivityLoopReverse/10000/20      3204984 ns      3204866 ns          219 RowInvRate=320.487ns
BM_IndexActivityLoopReverse/20000/20      6385054 ns      6384752 ns          111 RowInvRate=319.238ns
BM_IndexActivityLoopReverse/10000/100    17175970 ns     17175111 ns           41 RowInvRate=1.71751us
BM_IndexActivityLoopReverse/20000/100    36299898 ns     36299078 ns           19 RowInvRate=1.81495us
BM_IndexActivityTypeForward/10000/20      5348276 ns      5347825 ns          131 RowInvRate=534.783ns
BM_IndexActivityTypeForward/20000/20     10660093 ns     10658776 ns           66 RowInvRate=532.939ns
BM_IndexActivityTypeForward/10000/100    31115546 ns     31114806 ns           23 RowInvRate=3.11148us
BM_IndexActivityTypeForward/20000/100    62161303 ns     62159188 ns           11 RowInvRate=3.10796us
BM_IndexActivityTypeReverse/10000/20      6075976 ns      6075732 ns          116 RowInvRate=607.573ns
BM_IndexActivityTypeReverse/20000/20     12151928 ns     12151903 ns           58 RowInvRate=607.595ns
BM_IndexActivityTypeReverse/10000/100    34491257 ns     34491380 ns           20 RowInvRate=3.44914us
BM_IndexActivityTypeReverse/20000/100    68818961 ns     68816380 ns           10 RowInvRate=3.44082us
*/

static void do_bench(benchmark::State& state, const std::string& mode, const std::string& direction) {
    const int num_rows = state.range(0);
    const int variant_length = state.range(1);

    const int num_distinct_activities = 100;
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_distinct_activities - 1);

    std::vector<std::string> values;
    values.reserve(num_distinct_activities);
    for (int i = 0; i < num_distinct_activities; i++) {
        values.push_back("Activity" + std::to_string(i));
    }

    auto gen_rand_element = [&]() { return Slice(values[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                                                        TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                        TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_ARRAY);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto variant_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto mode_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), false);
        auto direction_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), false);
        mode_column->append_datum(mode.data());
        direction_column->append_datum(direction.data());
        mode_column = ConstColumn::create(mode_column, num_rows);
        direction_column = ConstColumn::create(direction_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            variant_column->append_datum(gen_rand_array(variant_length));
        }
        ctx->set_constant_columns({nullptr, mode_column, direction_column});

        state.ResumeTiming();
        ASSERT_TRUE(
                CelonisIndexActivity::celonis_index_activity_prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(
                CelonisIndexActivity::celonis_index_activity_prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(
                CelonisIndexActivity::celonis_index_activity(ctx.get(), {variant_column, mode_column, direction_column})
                        .ok());
        ASSERT_TRUE(
                CelonisIndexActivity::celonis_index_activity_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisIndexActivity::celonis_index_activity_close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_IndexActivityOrderForward(benchmark::State& state) {
    do_bench(state, "INDEX_ACTIVITY_ORDER", "FORWARD");
}

static void BM_IndexActivityOrderReverse(benchmark::State& state) {
    do_bench(state, "INDEX_ACTIVITY_ORDER", "REVERSE");
}

static void BM_IndexActivityLoopForward(benchmark::State& state) {
    do_bench(state, "INDEX_ACTIVITY_LOOP", "FORWARD");
}

static void BM_IndexActivityLoopReverse(benchmark::State& state) {
    do_bench(state, "INDEX_ACTIVITY_LOOP", "REVERSE");
}

static void BM_IndexActivityTypeForward(benchmark::State& state) {
    do_bench(state, "INDEX_ACTIVITY_TYPE", "FORWARD");
}

static void BM_IndexActivityTypeReverse(benchmark::State& state) {
    do_bench(state, "INDEX_ACTIVITY_TYPE", "REVERSE");
}

// Number of rows / Variant length
BENCHMARK(BM_IndexActivityOrderForward)->ArgsProduct({{10000, 20000}, {20, 100}});

BENCHMARK(BM_IndexActivityOrderReverse)->ArgsProduct({{10000, 20000}, {20, 100}});

BENCHMARK(BM_IndexActivityLoopForward)->ArgsProduct({{10000, 20000}, {20, 100}});

BENCHMARK(BM_IndexActivityLoopReverse)->ArgsProduct({{10000, 20000}, {20, 100}});

BENCHMARK(BM_IndexActivityTypeForward)->ArgsProduct({{10000, 20000}, {20, 100}});

BENCHMARK(BM_IndexActivityTypeReverse)->ArgsProduct({{10000, 20000}, {20, 100}});

} // namespace starrocks

BENCHMARK_MAIN();
