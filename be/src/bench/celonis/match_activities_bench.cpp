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
2024-09-24T20:46:40+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.62, 0.84, 1.35
Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
--------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5/0/0/0/0/0                      768984 ns       768897 ns          912 RowInvRate=768.897ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/5/0/0/0/0/0                    7584401 ns      7583915 ns           92 RowInvRate=758.391ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/5/0/0/0/0/0                  93453935 ns     93447962 ns            7 RowInvRate=934.48ns
BM_MatchActivitiesNonConstantConfig/1000/20/40/5/0/0/0/0/0                      717721 ns       717562 ns          975 RowInvRate=717.562ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/5/0/0/0/0/0                    7084373 ns      7082239 ns           95 RowInvRate=708.224ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/5/0/0/0/0/0                  88204577 ns     88196923 ns            8 RowInvRate=881.969ns
BM_MatchActivitiesNonConstantConfig/1000/20/60/5/0/0/0/0/0                      699643 ns       699529 ns         1003 RowInvRate=699.529ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5/0/0/0/0/0                    6894187 ns      6893256 ns          102 RowInvRate=689.326ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5/0/0/0/0/0                  86427817 ns     86418880 ns            8 RowInvRate=864.189ns
BM_MatchActivitiesNonConstantConfig/1000/20/20/10/0/0/0/0/0                    1130493 ns      1130417 ns          620 RowInvRate=1.13042us
BM_MatchActivitiesNonConstantConfig/10000/20/20/10/0/0/0/0/0                  11257911 ns     11256139 ns           62 RowInvRate=1.12561us
BM_MatchActivitiesNonConstantConfig/100000/20/20/10/0/0/0/0/0                130068879 ns    130051877 ns            5 RowInvRate=1.30052us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10/0/0/0/0/0                    1085158 ns      1084638 ns          646 RowInvRate=1084.64ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/10/0/0/0/0/0                  10739834 ns     10737876 ns           64 RowInvRate=1073.79ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/10/0/0/0/0/0                126109038 ns    126099385 ns            6 RowInvRate=1.26099us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10/0/0/0/0/0                    1053114 ns      1052857 ns          665 RowInvRate=1052.86ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/10/0/0/0/0/0                  10416130 ns     10415488 ns           66 RowInvRate=1041.55ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/10/0/0/0/0/0                123018903 ns    122996836 ns            6 RowInvRate=1.22997us
BM_MatchActivitiesConstantConfig/1000/20/20/5/0/0/0/0/0                         139273 ns       139212 ns         5049 RowInvRate=139.212ns
BM_MatchActivitiesConstantConfig/10000/20/20/5/0/0/0/0/0                       1370204 ns      1369993 ns          508 RowInvRate=136.999ns
BM_MatchActivitiesConstantConfig/100000/20/20/5/0/0/0/0/0                     31842037 ns     31838851 ns           23 RowInvRate=318.389ns
BM_MatchActivitiesConstantConfig/1000/20/40/5/0/0/0/0/0                          94722 ns        94622 ns         6730 RowInvRate=94.6218ns
BM_MatchActivitiesConstantConfig/10000/20/40/5/0/0/0/0/0                        945114 ns       944975 ns          778 RowInvRate=94.4975ns
BM_MatchActivitiesConstantConfig/100000/20/40/5/0/0/0/0/0                     26983174 ns     26978788 ns           26 RowInvRate=269.788ns
BM_MatchActivitiesConstantConfig/1000/20/60/5/0/0/0/0/0                          77483 ns        77440 ns         9087 RowInvRate=77.4397ns
BM_MatchActivitiesConstantConfig/10000/20/60/5/0/0/0/0/0                        747971 ns       747947 ns          930 RowInvRate=74.7947ns
BM_MatchActivitiesConstantConfig/100000/20/60/5/0/0/0/0/0                     24591609 ns     24587515 ns           28 RowInvRate=245.875ns
BM_MatchActivitiesConstantConfig/1000/20/20/10/0/0/0/0/0                        207449 ns       207394 ns         3404 RowInvRate=207.394ns
BM_MatchActivitiesConstantConfig/10000/20/20/10/0/0/0/0/0                      2073506 ns      2073324 ns          334 RowInvRate=207.332ns
BM_MatchActivitiesConstantConfig/100000/20/20/10/0/0/0/0/0                    37376141 ns     37370653 ns           19 RowInvRate=373.707ns
BM_MatchActivitiesConstantConfig/1000/20/40/10/0/0/0/0/0                        131636 ns       131523 ns         5316 RowInvRate=131.523ns
BM_MatchActivitiesConstantConfig/10000/20/40/10/0/0/0/0/0                      1296131 ns      1295855 ns          557 RowInvRate=129.585ns
BM_MatchActivitiesConstantConfig/100000/20/40/10/0/0/0/0/0                    29144042 ns     29136099 ns           24 RowInvRate=291.361ns
BM_MatchActivitiesConstantConfig/1000/20/60/10/0/0/0/0/0                        103893 ns       103838 ns         6825 RowInvRate=103.838ns
BM_MatchActivitiesConstantConfig/10000/20/60/10/0/0/0/0/0                      1020115 ns      1019940 ns          695 RowInvRate=101.994ns
BM_MatchActivitiesConstantConfig/100000/20/60/10/0/0/0/0/0                    26896206 ns     26892054 ns           26 RowInvRate=268.921ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000/0/0/0/0/0        313295 ns       313174 ns         2244 RowInvRate=313.174ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000/0/0/0/0/0      1582928 ns      1582301 ns          449 RowInvRate=158.23ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000/0/0/0/0/0    31572949 ns     31565171 ns           22 RowInvRate=315.652ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000/0/0/0/0/0  335603122 ns    335553908 ns            2 RowInvRate=335.554ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/5/0/0/0/0                         536563 ns       536450 ns         1304 RowInvRate=536.45ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/5/0/0/0/0                       5306494 ns      5306155 ns          129 RowInvRate=530.616ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/5/0/0/0/0                     69781121 ns     69771857 ns           10 RowInvRate=697.719ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/5/0/0/0/0                         430045 ns       429987 ns         1621 RowInvRate=429.987ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/5/0/0/0/0                       4307354 ns      4305337 ns          164 RowInvRate=430.534ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/5/0/0/0/0                     59664390 ns     59650477 ns           12 RowInvRate=596.505ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/5/0/0/0/0                         386749 ns       386539 ns         1810 RowInvRate=386.539ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/5/0/0/0/0                       3882224 ns      3881955 ns          182 RowInvRate=388.196ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/5/0/0/0/0                     55114148 ns     55102398 ns           13 RowInvRate=551.024ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/10/0/0/0/0                        574977 ns       574892 ns         1221 RowInvRate=574.892ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/10/0/0/0/0                      5713282 ns      5712792 ns          124 RowInvRate=571.279ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/10/0/0/0/0                    73304627 ns     73299232 ns            9 RowInvRate=732.992ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/10/0/0/0/0                        415023 ns       414970 ns         1690 RowInvRate=414.97ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/10/0/0/0/0                      4110230 ns      4110005 ns          171 RowInvRate=411.001ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/10/0/0/0/0                    57665262 ns     57655580 ns           12 RowInvRate=576.556ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/10/0/0/0/0                        348429 ns       348345 ns         2007 RowInvRate=348.345ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/10/0/0/0/0                      3456141 ns      3455749 ns          202 RowInvRate=345.575ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/10/0/0/0/0                    51339353 ns     51328523 ns           14 RowInvRate=513.285ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/5/0                         583335 ns       583268 ns         1190 RowInvRate=583.268ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/5/0                       5794453 ns      5792236 ns          118 RowInvRate=579.224ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/5/0                     73651364 ns     73638270 ns            9 RowInvRate=736.383ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/5/0                         496360 ns       496267 ns         1412 RowInvRate=496.267ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/5/0                       4945821 ns      4945202 ns          138 RowInvRate=494.52ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/5/0                     66031029 ns     66015579 ns           10 RowInvRate=660.156ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/5/0                         456454 ns       456325 ns         1534 RowInvRate=456.325ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/5/0                       4524291 ns      4523722 ns          155 RowInvRate=452.372ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/5/0                     61646388 ns     61639700 ns           11 RowInvRate=616.397ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/10/0                        676616 ns       676540 ns         1029 RowInvRate=676.54ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/10/0                      6818402 ns      6817547 ns          106 RowInvRate=681.755ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/10/0                    84802984 ns     84790853 ns            8 RowInvRate=847.909ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/10/0                        564679 ns       564426 ns         1232 RowInvRate=564.426ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/10/0                      5575884 ns      5573614 ns          125 RowInvRate=557.361ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/10/0                    72021929 ns     72006349 ns            9 RowInvRate=720.063ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/10/0                        506293 ns       506226 ns         1383 RowInvRate=506.226ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/10/0                      5002114 ns      5001281 ns          100 RowInvRate=500.128ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/10/0                    66932218 ns     66924205 ns           10 RowInvRate=669.242ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/5                         383070 ns       382934 ns         1828 RowInvRate=382.934ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/5                       3833998 ns      3833190 ns          183 RowInvRate=383.319ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/5                     54836475 ns     54831162 ns           12 RowInvRate=548.312ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/5                         364065 ns       363892 ns         1925 RowInvRate=363.892ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/5                       3611203 ns      3610733 ns          194 RowInvRate=361.073ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/5                     53019857 ns     53012234 ns           13 RowInvRate=530.122ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/5                         355880 ns       355747 ns         1967 RowInvRate=355.747ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/5                       3556121 ns      3555632 ns          194 RowInvRate=355.563ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/5                     51884189 ns     51878124 ns           13 RowInvRate=518.781ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/10                        376980 ns       376728 ns         1860 RowInvRate=376.728ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/10                      3749305 ns      3748606 ns          187 RowInvRate=374.861ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/10                    54275454 ns     54265582 ns           13 RowInvRate=542.656ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/10                        355592 ns       355498 ns         1970 RowInvRate=355.498ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/10                      3562104 ns      3561735 ns          197 RowInvRate=356.173ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/10                    52729410 ns     52723127 ns           13 RowInvRate=527.231ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/10                        354335 ns       353988 ns         2017 RowInvRate=353.988ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/10                      3497267 ns      3497037 ns          199 RowInvRate=349.704ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/10                    52625790 ns     52620793 ns           13 RowInvRate=526.208ns
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
// STARTING nodes
BENCHMARK(BM_MatchActivitiesNonConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantLargeMatchesConfig)->ArgsProduct({{1000, 10000, 100000, 1000000}, {20}, {5000}, {3000}, {0}, {0}, {0}, {0}, {0}});
// NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {5, 10}, {0}, {0}, {0}, {0}});
// EXCLUDING_ALL nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {5, 10}, {0}});
// ANY_NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {0}, {5, 10}});


} // namespace starrocks

BENCHMARK_MAIN();

