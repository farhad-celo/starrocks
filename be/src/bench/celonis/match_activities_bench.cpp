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

// Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
/*
2025-03-03T02:55:41+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.35, 2.35, 1.95
--------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5/0/0/0/0/0                      812108 ns       812087 ns          859 RowInvRate=812.087ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/5/0/0/0/0/0                    7877648 ns      7877310 ns           89 RowInvRate=787.731ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/5/0/0/0/0/0                  82621069 ns     82615761 ns            9 RowInvRate=826.158ns
BM_MatchActivitiesNonConstantConfig/1000/20/40/5/0/0/0/0/0                      780335 ns       780298 ns          903 RowInvRate=780.298ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/5/0/0/0/0/0                    7616298 ns      7615654 ns           95 RowInvRate=761.565ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/5/0/0/0/0/0                  80933549 ns     80932366 ns            9 RowInvRate=809.324ns
BM_MatchActivitiesNonConstantConfig/1000/20/60/5/0/0/0/0/0                      758898 ns       758886 ns          915 RowInvRate=758.886ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5/0/0/0/0/0                    7204707 ns      7203850 ns           97 RowInvRate=720.385ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5/0/0/0/0/0                  74206771 ns     74204758 ns            9 RowInvRate=742.048ns
BM_MatchActivitiesNonConstantConfig/1000/20/20/10/0/0/0/0/0                    1218360 ns      1218335 ns          575 RowInvRate=1.21834us
BM_MatchActivitiesNonConstantConfig/10000/20/20/10/0/0/0/0/0                  12010434 ns     12010072 ns           58 RowInvRate=1.20101us
BM_MatchActivitiesNonConstantConfig/100000/20/20/10/0/0/0/0/0                122617042 ns    122615240 ns            6 RowInvRate=1.22615us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10/0/0/0/0/0                    1141488 ns      1141452 ns          612 RowInvRate=1.14145us
BM_MatchActivitiesNonConstantConfig/10000/20/40/10/0/0/0/0/0                  11264808 ns     11264562 ns           62 RowInvRate=1.12646us
BM_MatchActivitiesNonConstantConfig/100000/20/40/10/0/0/0/0/0                115307273 ns    115303999 ns            6 RowInvRate=1.15304us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10/0/0/0/0/0                    1103348 ns      1103302 ns          634 RowInvRate=1.1033us
BM_MatchActivitiesNonConstantConfig/10000/20/60/10/0/0/0/0/0                  10904687 ns     10904508 ns           64 RowInvRate=1090.45ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/10/0/0/0/0/0                111656642 ns    111651287 ns            6 RowInvRate=1.11651us
BM_MatchActivitiesConstantConfig/1000/20/20/5/0/0/0/0/0                         153376 ns       153372 ns         4571 RowInvRate=153.372ns
BM_MatchActivitiesConstantConfig/10000/20/20/5/0/0/0/0/0                       1520745 ns      1520711 ns          465 RowInvRate=152.071ns
BM_MatchActivitiesConstantConfig/100000/20/20/5/0/0/0/0/0                     17510050 ns     17494003 ns           41 RowInvRate=174.94ns
BM_MatchActivitiesConstantConfig/1000/20/40/5/0/0/0/0/0                         103135 ns       103143 ns         6850 RowInvRate=103.143ns
BM_MatchActivitiesConstantConfig/10000/20/40/5/0/0/0/0/0                        999802 ns       999745 ns          703 RowInvRate=99.9745ns
BM_MatchActivitiesConstantConfig/100000/20/40/5/0/0/0/0/0                     12600634 ns     12599920 ns           55 RowInvRate=125.999ns
BM_MatchActivitiesConstantConfig/1000/20/60/5/0/0/0/0/0                          85949 ns        85954 ns         8224 RowInvRate=85.9543ns
BM_MatchActivitiesConstantConfig/10000/20/60/5/0/0/0/0/0                        820277 ns       820255 ns          855 RowInvRate=82.0255ns
BM_MatchActivitiesConstantConfig/100000/20/60/5/0/0/0/0/0                     10984479 ns     10984371 ns           65 RowInvRate=109.844ns
BM_MatchActivitiesConstantConfig/1000/20/20/10/0/0/0/0/0                        229988 ns       229871 ns         3026 RowInvRate=229.871ns
BM_MatchActivitiesConstantConfig/10000/20/20/10/0/0/0/0/0                      2279751 ns      2279645 ns          308 RowInvRate=227.965ns
BM_MatchActivitiesConstantConfig/100000/20/20/10/0/0/0/0/0                    25457147 ns     25453060 ns           29 RowInvRate=254.531ns
BM_MatchActivitiesConstantConfig/1000/20/40/10/0/0/0/0/0                        145612 ns       145620 ns         4793 RowInvRate=145.62ns
BM_MatchActivitiesConstantConfig/10000/20/40/10/0/0/0/0/0                      1419737 ns      1419707 ns          496 RowInvRate=141.971ns
BM_MatchActivitiesConstantConfig/100000/20/40/10/0/0/0/0/0                    16514793 ns     16512690 ns           42 RowInvRate=165.127ns
BM_MatchActivitiesConstantConfig/1000/20/60/10/0/0/0/0/0                        115008 ns       114998 ns         6110 RowInvRate=114.998ns
BM_MatchActivitiesConstantConfig/10000/20/60/10/0/0/0/0/0                      1115102 ns      1115145 ns          633 RowInvRate=111.514ns
BM_MatchActivitiesConstantConfig/100000/20/60/10/0/0/0/0/0                    13853375 ns     13852149 ns           51 RowInvRate=138.521ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000/0/0/0/0/0        354185 ns       354163 ns         1974 RowInvRate=354.163ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000/0/0/0/0/0      1826259 ns      1825907 ns          379 RowInvRate=182.591ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000/0/0/0/0/0    19399450 ns     19397997 ns           36 RowInvRate=193.98ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000/0/0/0/0/0  191925565 ns    191913903 ns            4 RowInvRate=191.914ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/5/0/0/0/0                         582840 ns       582599 ns         1206 RowInvRate=582.599ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/5/0/0/0/0                       5806099 ns      5806065 ns          121 RowInvRate=580.606ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/5/0/0/0/0                     60819383 ns     60814277 ns           12 RowInvRate=608.143ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/5/0/0/0/0                         470496 ns       470464 ns         1488 RowInvRate=470.464ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/5/0/0/0/0                       4641212 ns      4640896 ns          149 RowInvRate=464.09ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/5/0/0/0/0                     49324825 ns     49323340 ns           14 RowInvRate=493.233ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/5/0/0/0/0                         422867 ns       422807 ns         1657 RowInvRate=422.807ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/5/0/0/0/0                       4201854 ns      4201698 ns          167 RowInvRate=420.17ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/5/0/0/0/0                     45501569 ns     45501314 ns           15 RowInvRate=455.013ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/10/0/0/0/0                        629789 ns       629759 ns         1103 RowInvRate=629.759ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/10/0/0/0/0                      6288005 ns      6287788 ns          111 RowInvRate=628.779ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/10/0/0/0/0                    65453210 ns     65452320 ns           11 RowInvRate=654.523ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/10/0/0/0/0                        453264 ns       453243 ns         1542 RowInvRate=453.243ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/10/0/0/0/0                      4503923 ns      4503804 ns          156 RowInvRate=450.38ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/10/0/0/0/0                    48022645 ns     48001791 ns           15 RowInvRate=480.018ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/10/0/0/0/0                        383368 ns       383349 ns         1828 RowInvRate=383.349ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/10/0/0/0/0                      3820472 ns      3820385 ns          184 RowInvRate=382.038ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/10/0/0/0/0                    40845001 ns     40841249 ns           17 RowInvRate=408.412ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/5/0/0/0                         155051 ns       155031 ns         4520 RowInvRate=155.031ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/5/0/0/0                       1541624 ns      1541525 ns          457 RowInvRate=154.153ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/5/0/0/0                     19212767 ns     19211943 ns           37 RowInvRate=192.119ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/5/0/0/0                         103280 ns       103274 ns         6839 RowInvRate=103.274ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/5/0/0/0                       1009776 ns      1009809 ns          701 RowInvRate=100.981ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/5/0/0/0                     13628228 ns     13627748 ns           52 RowInvRate=136.277ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/5/0/0/0                          85496 ns        85486 ns         8261 RowInvRate=85.4858ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/5/0/0/0                        827813 ns       827848 ns          859 RowInvRate=82.7848ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/5/0/0/0                     11848013 ns     11847388 ns           60 RowInvRate=118.474ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/10/0/0/0                        230962 ns       230952 ns         3024 RowInvRate=230.952ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/10/0/0/0                      2286862 ns      2286763 ns          305 RowInvRate=228.676ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/10/0/0/0                    25801002 ns     25798951 ns           27 RowInvRate=257.99ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/10/0/0/0                        147146 ns       147142 ns         4795 RowInvRate=147.142ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/10/0/0/0                      1456624 ns      1456603 ns          489 RowInvRate=145.66ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/10/0/0/0                    17824022 ns     17823773 ns           38 RowInvRate=178.238ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/10/0/0/0                        115247 ns       115246 ns         6083 RowInvRate=115.246ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/10/0/0/0                      1127138 ns      1127245 ns          627 RowInvRate=112.724ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/10/0/0/0                    15179285 ns     15179036 ns           47 RowInvRate=151.79ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/5/0                         634625 ns       634596 ns         1098 RowInvRate=634.596ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/5/0                       6338221 ns      6337654 ns          111 RowInvRate=633.765ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/5/0                     66625332 ns     66624459 ns           11 RowInvRate=666.245ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/5/0                         537561 ns       537517 ns         1296 RowInvRate=537.517ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/5/0                       5321654 ns      5320977 ns          133 RowInvRate=532.098ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/5/0                     56553203 ns     56552554 ns           12 RowInvRate=565.526ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/5/0                         495880 ns       495875 ns         1414 RowInvRate=495.875ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/5/0                       4942054 ns      4942032 ns          139 RowInvRate=494.203ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/5/0                     52673870 ns     52668580 ns           14 RowInvRate=526.686ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/10/0                        744937 ns       744823 ns          940 RowInvRate=744.823ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/10/0                      7402279 ns      7401490 ns           93 RowInvRate=740.149ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/10/0                    77793491 ns     77777171 ns            9 RowInvRate=777.772ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/10/0                        617186 ns       617146 ns         1127 RowInvRate=617.146ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/10/0                      6116267 ns      6116119 ns          114 RowInvRate=611.612ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/10/0                    64587273 ns     64579044 ns           11 RowInvRate=645.79ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/10/0                        554918 ns       554867 ns         1260 RowInvRate=554.867ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/10/0                      5491017 ns      5490685 ns          127 RowInvRate=549.068ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/10/0                    57592613 ns     57587515 ns           12 RowInvRate=575.875ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/5                         153066 ns       153057 ns         4580 RowInvRate=153.057ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/5                       1505822 ns      1505712 ns          458 RowInvRate=150.571ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/5                     18054526 ns     18053810 ns           39 RowInvRate=180.538ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/5                         209975 ns       209966 ns         3351 RowInvRate=209.966ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/5                       2051314 ns      2051287 ns          341 RowInvRate=205.129ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/5                     23601526 ns     23600875 ns           29 RowInvRate=236.009ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/5                         243410 ns       243401 ns         2867 RowInvRate=243.401ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/5                       2406211 ns      2406045 ns          292 RowInvRate=240.604ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/5                     26768800 ns     26767813 ns           25 RowInvRate=267.678ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/10                        115714 ns       115734 ns         6061 RowInvRate=115.734ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/10                      1137076 ns      1137053 ns          623 RowInvRate=113.705ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/10                    14239062 ns     14237023 ns           49 RowInvRate=142.37ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/10                        152251 ns       152232 ns         4663 RowInvRate=152.232ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/10                      1468099 ns      1468092 ns          475 RowInvRate=146.809ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/10                    17565717 ns     17564523 ns           40 RowInvRate=175.645ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/10                        179807 ns       179823 ns         3919 RowInvRate=179.823ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/10                      1799003 ns      1798905 ns          386 RowInvRate=179.89ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/10                    20992537 ns     20989855 ns           34 RowInvRate=209.899ns
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
// ENDING nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {5, 10}, {0}, {0}, {0}});
// EXCLUDING_ALL nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {5, 10}, {0}});
// ANY_NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {0}, {5, 10}});


} // namespace starrocks

BENCHMARK_MAIN();

