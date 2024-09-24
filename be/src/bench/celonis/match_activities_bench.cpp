#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/match_activities.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2024-09-24T11:38:13+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.30, 2.24, 2.02
Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
--------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5/0/0/0/0/0                      726575 ns       726380 ns          962 RowInvRate=726.38ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/5/0/0/0/0/0                    7203587 ns      7201647 ns           94 RowInvRate=720.165ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/5/0/0/0/0/0                  88331267 ns     88320049 ns            8 RowInvRate=883.2ns
BM_MatchActivitiesNonConstantConfig/1000/20/40/5/0/0/0/0/0                      673721 ns       673427 ns         1038 RowInvRate=673.427ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/5/0/0/0/0/0                    6725705 ns      6725238 ns          104 RowInvRate=672.524ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/5/0/0/0/0/0                  83463436 ns     83451618 ns            8 RowInvRate=834.516ns
BM_MatchActivitiesNonConstantConfig/1000/20/60/5/0/0/0/0/0                      654212 ns       654025 ns         1070 RowInvRate=654.025ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5/0/0/0/0/0                    6496427 ns      6495883 ns          106 RowInvRate=649.588ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5/0/0/0/0/0                  81662924 ns     81652008 ns            8 RowInvRate=816.52ns
BM_MatchActivitiesNonConstantConfig/1000/20/20/10/0/0/0/0/0                    1092171 ns      1091717 ns          635 RowInvRate=1091.72ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/10/0/0/0/0/0                  10877792 ns     10874808 ns           63 RowInvRate=1087.48ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/10/0/0/0/0/0                125288166 ns    125274205 ns            6 RowInvRate=1.25274us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10/0/0/0/0/0                    1024559 ns      1024077 ns          684 RowInvRate=1024.08ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/10/0/0/0/0/0                  10212568 ns     10211522 ns           69 RowInvRate=1021.15ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/10/0/0/0/0/0                118298309 ns    118285106 ns            6 RowInvRate=1.18285us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10/0/0/0/0/0                     988242 ns       987667 ns          706 RowInvRate=987.667ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/10/0/0/0/0/0                   9824092 ns      9823166 ns           71 RowInvRate=982.317ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/10/0/0/0/0/0                115321093 ns    115306740 ns            6 RowInvRate=1.15307us
BM_MatchActivitiesConstantConfig/1000/20/20/5/0/0/0/0/0                         134706 ns       134596 ns         5184 RowInvRate=134.596ns
BM_MatchActivitiesConstantConfig/10000/20/20/5/0/0/0/0/0                       1351604 ns      1351493 ns          517 RowInvRate=135.149ns
BM_MatchActivitiesConstantConfig/100000/20/20/5/0/0/0/0/0                     29753627 ns     29749527 ns           24 RowInvRate=297.495ns
BM_MatchActivitiesConstantConfig/1000/20/40/5/0/0/0/0/0                          91427 ns        91362 ns         7690 RowInvRate=91.3621ns
BM_MatchActivitiesConstantConfig/10000/20/40/5/0/0/0/0/0                        901323 ns       901170 ns          796 RowInvRate=90.117ns
BM_MatchActivitiesConstantConfig/100000/20/40/5/0/0/0/0/0                     25375193 ns     25370943 ns           28 RowInvRate=253.709ns
BM_MatchActivitiesConstantConfig/1000/20/60/5/0/0/0/0/0                          76023 ns        75925 ns         9202 RowInvRate=75.9245ns
BM_MatchActivitiesConstantConfig/10000/20/60/5/0/0/0/0/0                        761828 ns       761703 ns          958 RowInvRate=76.1703ns
BM_MatchActivitiesConstantConfig/100000/20/60/5/0/0/0/0/0                     23931778 ns     23929029 ns           29 RowInvRate=239.29ns
BM_MatchActivitiesConstantConfig/1000/20/20/10/0/0/0/0/0                        202319 ns       202184 ns         3459 RowInvRate=202.184ns
BM_MatchActivitiesConstantConfig/10000/20/20/10/0/0/0/0/0                      1992472 ns      1992076 ns          353 RowInvRate=199.208ns
BM_MatchActivitiesConstantConfig/100000/20/20/10/0/0/0/0/0                    35394120 ns     35390691 ns           20 RowInvRate=353.907ns
BM_MatchActivitiesConstantConfig/1000/20/40/10/0/0/0/0/0                        129392 ns       129313 ns         5453 RowInvRate=129.313ns
BM_MatchActivitiesConstantConfig/10000/20/40/10/0/0/0/0/0                      1288357 ns      1288166 ns          561 RowInvRate=128.817ns
BM_MatchActivitiesConstantConfig/100000/20/40/10/0/0/0/0/0                    29021055 ns     29018435 ns           24 RowInvRate=290.184ns
BM_MatchActivitiesConstantConfig/1000/20/60/10/0/0/0/0/0                        102465 ns       102407 ns         6812 RowInvRate=102.407ns
BM_MatchActivitiesConstantConfig/10000/20/60/10/0/0/0/0/0                       995884 ns       995783 ns          702 RowInvRate=99.5783ns
BM_MatchActivitiesConstantConfig/100000/20/60/10/0/0/0/0/0                    26399498 ns     26397671 ns           27 RowInvRate=263.977ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000/0/0/0/0/0        306314 ns       306212 ns         2287 RowInvRate=306.212ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000/0/0/0/0/0      1548053 ns      1547787 ns          450 RowInvRate=154.779ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000/0/0/0/0/0    30829554 ns     30824642 ns           23 RowInvRate=308.246ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000/0/0/0/0/0  320120132 ns    320080867 ns            2 RowInvRate=320.081ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/5/0                         613645 ns       613431 ns         1141 RowInvRate=613.431ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/5/0                       6151608 ns      6151042 ns          112 RowInvRate=615.104ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/5/0                     77379160 ns     77372583 ns            9 RowInvRate=773.726ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/5/0                         501209 ns       501138 ns         1386 RowInvRate=501.138ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/5/0                       5016032 ns      5015556 ns          141 RowInvRate=501.556ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/5/0                     66300531 ns     66293244 ns           10 RowInvRate=662.932ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/5/0                         450142 ns       449956 ns         1557 RowInvRate=449.956ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/5/0                       4495812 ns      4495365 ns          155 RowInvRate=449.537ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/5/0                     60996406 ns     60990367 ns           11 RowInvRate=609.904ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/10/0                        760417 ns       760318 ns          915 RowInvRate=760.318ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/10/0                      7621121 ns      7618448 ns           92 RowInvRate=761.845ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/10/0                    91024484 ns     91013275 ns            8 RowInvRate=910.133ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/10/0                        612922 ns       612650 ns         1143 RowInvRate=612.65ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/10/0                      6156512 ns      6155497 ns          110 RowInvRate=615.55ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/10/0                    75680465 ns     75672830 ns            9 RowInvRate=756.728ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/10/0                        531754 ns       531545 ns         1312 RowInvRate=531.545ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/10/0                      5276872 ns      5276551 ns          128 RowInvRate=527.655ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/10/0                    69519924 ns     69506767 ns           10 RowInvRate=695.068ns
*/

enum MatchType {
    CONSTANT,
    NON_CONSTANT,
};

static void do_bench(benchmark::State& state, MatchType match_type) {
    int num_rows = state.range(0);
    int variant_length = state.range(1);
    int num_values = state.range(2);
    // STARTING, NODE, ENDING, EXCLUDING, EXCLUDING_ALL, NODES_ANY
    int starting_nodes_length = state.range(3);
    int nodes_length = state.range(4);
    int ending_nodes_length = state.range(5);
    int excluding_nodes_length = state.range(6);
    int excluding_all_nodes_length = state.range(7);
    int nodes_any_length = state.range(8);


    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_values - 1);

    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    auto gen_rand_element = [&]() { return Slice(values[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _: state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto variant_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        for (int i = 0; i < num_rows; i++) {
            variant_column->append_datum(gen_rand_array(variant_length));
        }
        auto starting_nodes_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto nodes_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto ending_nodes_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto excluding_nodes_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto excluding_all_nodes_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto any_nodes_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        switch (match_type) {
            case CONSTANT:
                starting_nodes_column->append_datum(gen_rand_array(starting_nodes_length));
                nodes_column->append_datum(gen_rand_array(nodes_length));
                ending_nodes_column->append_datum(gen_rand_array(ending_nodes_length));
                excluding_nodes_column->append_datum(gen_rand_array(excluding_nodes_length));
                excluding_all_nodes_column->append_datum(gen_rand_array(excluding_all_nodes_length));
                any_nodes_column->append_datum(gen_rand_array(nodes_any_length));
                ctx->set_constant_columns(
                        {nullptr, starting_nodes_column, nodes_column, ending_nodes_column, excluding_nodes_column,
                         excluding_all_nodes_column, any_nodes_column});
                break;
            case NON_CONSTANT:
                for (int i = 0; i < num_rows; i++) {
                    starting_nodes_column->append_datum(gen_rand_array(starting_nodes_length));
                    nodes_column->append_datum(gen_rand_array(nodes_length));
                    ending_nodes_column->append_datum(gen_rand_array(ending_nodes_length));
                    excluding_nodes_column->append_datum(gen_rand_array(excluding_nodes_length));
                    excluding_all_nodes_column->append_datum(gen_rand_array(excluding_all_nodes_length));
                    any_nodes_column->append_datum(gen_rand_array(nodes_any_length));
                }
                ctx->set_constant_columns({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
                break;
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisMatchActivitiesFunctions::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisMatchActivitiesFunctions::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisMatchActivitiesFunctions::celonis_match_activities(ctx.get(),
                                                                              {variant_column, starting_nodes_column,
                                                                               nodes_column, ending_nodes_column,
                                                                               excluding_nodes_column,
                                                                               excluding_all_nodes_column,
                                                                               any_nodes_column}).ok());
        ASSERT_TRUE(CelonisMatchActivitiesFunctions::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisMatchActivitiesFunctions::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_MatchActivitiesNonConstantConfig(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

static void BM_MatchActivitiesConstantConfig(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

static void BM_MatchActivitiesConstantLargeMatchesConfig(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

// Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
// Starting nodes
BENCHMARK(BM_MatchActivitiesNonConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantLargeMatchesConfig)->ArgsProduct({{1000, 10000, 100000, 1000000}, {20}, {5000}, {3000}, {0}, {0}, {0}, {0}, {0}});
// Excluding all nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {5, 10}, {0}});

} // namespace starrocks

BENCHMARK_MAIN();

