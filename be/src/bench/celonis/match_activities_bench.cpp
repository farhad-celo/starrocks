#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/match_activities.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-08-18T11:05:35+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.19, 3.02, 3.03
// Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
--------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5/0/0/0/0/0                      866668 ns       866658 ns          817 RowInvRate=866.658ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/5/0/0/0/0/0                    8142504 ns      8142027 ns           87 RowInvRate=814.203ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/5/0/0/0/0/0                  81325092 ns     81321426 ns            9 RowInvRate=813.214ns
BM_MatchActivitiesNonConstantConfig/1000/20/40/5/0/0/0/0/0                      785807 ns       785679 ns          869 RowInvRate=785.679ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/5/0/0/0/0/0                    7591666 ns      7591418 ns           92 RowInvRate=759.142ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/5/0/0/0/0/0                  75885736 ns     75885336 ns            9 RowInvRate=758.853ns
BM_MatchActivitiesNonConstantConfig/1000/20/60/5/0/0/0/0/0                      781073 ns       780780 ns          888 RowInvRate=780.78ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5/0/0/0/0/0                    7332326 ns      7332136 ns           97 RowInvRate=733.214ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5/0/0/0/0/0                  73901208 ns     73897540 ns           10 RowInvRate=738.975ns
BM_MatchActivitiesNonConstantConfig/1000/20/20/10/0/0/0/0/0                    1279917 ns      1279862 ns          545 RowInvRate=1.27986us
BM_MatchActivitiesNonConstantConfig/10000/20/20/10/0/0/0/0/0                  12635696 ns     12634703 ns           56 RowInvRate=1.26347us
BM_MatchActivitiesNonConstantConfig/100000/20/20/10/0/0/0/0/0                125596553 ns    125596070 ns            6 RowInvRate=1.25596us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10/0/0/0/0/0                    1211864 ns      1211789 ns          574 RowInvRate=1.21179us
BM_MatchActivitiesNonConstantConfig/10000/20/40/10/0/0/0/0/0                  11815271 ns     11814772 ns           58 RowInvRate=1.18148us
BM_MatchActivitiesNonConstantConfig/100000/20/40/10/0/0/0/0/0                120232204 ns    120229660 ns            6 RowInvRate=1.2023us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10/0/0/0/0/0                    1182933 ns      1182804 ns          592 RowInvRate=1.1828us
BM_MatchActivitiesNonConstantConfig/10000/20/60/10/0/0/0/0/0                  11542652 ns     11541391 ns           61 RowInvRate=1.15414us
BM_MatchActivitiesNonConstantConfig/100000/20/60/10/0/0/0/0/0                114481579 ns    114470719 ns            6 RowInvRate=1.14471us
BM_MatchActivitiesConstantConfig/1000/20/20/5/0/0/0/0/0                         156063 ns       156051 ns         4474 RowInvRate=156.051ns
BM_MatchActivitiesConstantConfig/10000/20/20/5/0/0/0/0/0                       1525001 ns      1524971 ns          457 RowInvRate=152.497ns
BM_MatchActivitiesConstantConfig/100000/20/20/5/0/0/0/0/0                     17579137 ns     17578088 ns           39 RowInvRate=175.781ns
BM_MatchActivitiesConstantConfig/1000/20/40/5/0/0/0/0/0                         104970 ns       104960 ns         6694 RowInvRate=104.96ns
BM_MatchActivitiesConstantConfig/10000/20/40/5/0/0/0/0/0                       1020910 ns      1020846 ns          681 RowInvRate=102.085ns
BM_MatchActivitiesConstantConfig/100000/20/40/5/0/0/0/0/0                     12705699 ns     12702976 ns           54 RowInvRate=127.03ns
BM_MatchActivitiesConstantConfig/1000/20/60/5/0/0/0/0/0                          87774 ns        87763 ns         8010 RowInvRate=87.7633ns
BM_MatchActivitiesConstantConfig/10000/20/60/5/0/0/0/0/0                        847398 ns       847384 ns          841 RowInvRate=84.7384ns
BM_MatchActivitiesConstantConfig/100000/20/60/5/0/0/0/0/0                     10984808 ns     10983468 ns           64 RowInvRate=109.835ns
BM_MatchActivitiesConstantConfig/1000/20/20/10/0/0/0/0/0                        232429 ns       232403 ns         3004 RowInvRate=232.403ns
BM_MatchActivitiesConstantConfig/10000/20/20/10/0/0/0/0/0                      2279959 ns      2279925 ns          302 RowInvRate=227.993ns
BM_MatchActivitiesConstantConfig/100000/20/20/10/0/0/0/0/0                    24685328 ns     24682399 ns           27 RowInvRate=246.824ns
BM_MatchActivitiesConstantConfig/1000/20/40/10/0/0/0/0/0                        148546 ns       148539 ns         4734 RowInvRate=148.539ns
BM_MatchActivitiesConstantConfig/10000/20/40/10/0/0/0/0/0                      1446502 ns      1446444 ns          484 RowInvRate=144.644ns
BM_MatchActivitiesConstantConfig/100000/20/40/10/0/0/0/0/0                    16975790 ns     16975358 ns           42 RowInvRate=169.754ns
BM_MatchActivitiesConstantConfig/1000/20/60/10/0/0/0/0/0                        117017 ns       117008 ns         5998 RowInvRate=117.008ns
BM_MatchActivitiesConstantConfig/10000/20/60/10/0/0/0/0/0                      1165718 ns      1165784 ns          631 RowInvRate=116.578ns
BM_MatchActivitiesConstantConfig/100000/20/60/10/0/0/0/0/0                    13848578 ns     13847486 ns           50 RowInvRate=138.475ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000/0/0/0/0/0        350268 ns       350228 ns         1997 RowInvRate=350.228ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000/0/0/0/0/0      1831536 ns      1831456 ns          381 RowInvRate=183.146ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000/0/0/0/0/0    19513459 ns     19513066 ns           36 RowInvRate=195.131ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000/0/0/0/0/0  405842966 ns    405659757 ns            2 RowInvRate=405.66ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/1/0/0/0/0                         774636 ns       773895 ns          888 RowInvRate=773.895ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/1/0/0/0/0                       7761666 ns      7755178 ns           87 RowInvRate=775.518ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/1/0/0/0/0                     36980502 ns     36980414 ns           19 RowInvRate=369.804ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/1/0/0/0/0                         380422 ns       380353 ns         1868 RowInvRate=380.353ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/1/0/0/0/0                       3779830 ns      3779832 ns          183 RowInvRate=377.983ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/1/0/0/0/0                     39737429 ns     39735324 ns           18 RowInvRate=397.353ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/1/0/0/0/0                         380500 ns       380495 ns         1840 RowInvRate=380.495ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/1/0/0/0/0                       3768035 ns      3767415 ns          185 RowInvRate=376.742ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/1/0/0/0/0                     40334202 ns     40333998 ns           17 RowInvRate=403.34ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/5/0/0/0/0                         581328 ns       581286 ns         1203 RowInvRate=581.286ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/5/0/0/0/0                       5783830 ns      5783354 ns          121 RowInvRate=578.335ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/5/0/0/0/0                     60149279 ns     60144964 ns           13 RowInvRate=601.45ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/5/0/0/0/0                         480689 ns       480673 ns         1455 RowInvRate=480.673ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/5/0/0/0/0                       4759348 ns      4759254 ns          146 RowInvRate=475.925ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/5/0/0/0/0                     50261118 ns     50255544 ns           14 RowInvRate=502.555ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/5/0/0/0/0                         433121 ns       433099 ns         1615 RowInvRate=433.099ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/5/0/0/0/0                       4314358 ns      4314255 ns          162 RowInvRate=431.426ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/5/0/0/0/0                     45872364 ns     45870120 ns           15 RowInvRate=458.701ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/10/0/0/0/0                        640645 ns       640624 ns         1090 RowInvRate=640.624ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/10/0/0/0/0                      6380779 ns      6380527 ns          110 RowInvRate=638.053ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/10/0/0/0/0                    66316873 ns     66315183 ns           11 RowInvRate=663.152ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/10/0/0/0/0                        464986 ns       464960 ns         1505 RowInvRate=464.96ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/10/0/0/0/0                      4600591 ns      4600275 ns          151 RowInvRate=460.028ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/10/0/0/0/0                    48888405 ns     48886070 ns           14 RowInvRate=488.861ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/10/0/0/0/0                        392914 ns       392901 ns         1781 RowInvRate=392.901ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/10/0/0/0/0                      3891526 ns      3891342 ns          180 RowInvRate=389.134ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/10/0/0/0/0                    41133163 ns     41129394 ns           17 RowInvRate=411.294ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/5/0/0/0                         158284 ns       158262 ns         4446 RowInvRate=158.262ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/5/0/0/0                       1564789 ns      1564718 ns          440 RowInvRate=156.472ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/5/0/0/0                     18835347 ns     18833952 ns           37 RowInvRate=188.34ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/5/0/0/0                         105481 ns       105468 ns         6604 RowInvRate=105.468ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/5/0/0/0                       1039298 ns      1039306 ns          679 RowInvRate=103.931ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/5/0/0/0                     13403822 ns     13402714 ns           52 RowInvRate=134.027ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/5/0/0/0                          87797 ns        87760 ns         7972 RowInvRate=87.7602ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/5/0/0/0                        854524 ns       854531 ns          817 RowInvRate=85.4531ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/5/0/0/0                     11688695 ns     11687248 ns           62 RowInvRate=116.872ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/10/0/0/0                        236157 ns       236148 ns         2905 RowInvRate=236.148ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/10/0/0/0                      2334087 ns      2334058 ns          296 RowInvRate=233.406ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/10/0/0/0                    25821171 ns     25820868 ns           26 RowInvRate=258.209ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/10/0/0/0                        149321 ns       149325 ns         4722 RowInvRate=149.325ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/10/0/0/0                      1467057 ns      1466990 ns          471 RowInvRate=146.699ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/10/0/0/0                    18247808 ns     18246484 ns           40 RowInvRate=182.465ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/10/0/0/0                        118777 ns       118762 ns         5883 RowInvRate=118.762ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/10/0/0/0                      1162024 ns      1161907 ns          604 RowInvRate=116.191ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/10/0/0/0                    14755489 ns     14755184 ns           48 RowInvRate=147.552ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/5/0                         638278 ns       638222 ns         1099 RowInvRate=638.222ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/5/0                       6341154 ns      6340377 ns          110 RowInvRate=634.038ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/5/0                     65776304 ns     65774740 ns           11 RowInvRate=657.747ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/5/0                         540881 ns       540840 ns         1295 RowInvRate=540.84ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/5/0                       5382148 ns      5381895 ns          130 RowInvRate=538.189ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/5/0                     57358741 ns     57355611 ns           13 RowInvRate=573.556ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/5/0                         498047 ns       497993 ns         1403 RowInvRate=497.993ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/5/0                       4924665 ns      4924371 ns          141 RowInvRate=492.437ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/5/0                     51865367 ns     51859339 ns           13 RowInvRate=518.593ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/10/0                        749110 ns       748801 ns          933 RowInvRate=748.801ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/10/0                      7442184 ns      7441344 ns           92 RowInvRate=744.134ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/10/0                    76773215 ns     76765280 ns           10 RowInvRate=767.653ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/10/0                        618444 ns       618419 ns         1120 RowInvRate=618.419ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/10/0                      6102805 ns      6102572 ns          114 RowInvRate=610.257ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/10/0                    64390266 ns     64389905 ns           11 RowInvRate=643.899ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/10/0                        553399 ns       553375 ns         1267 RowInvRate=553.375ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/10/0                      5501999 ns      5501182 ns          128 RowInvRate=550.118ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/10/0                    58116385 ns     58112060 ns           12 RowInvRate=581.121ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/5                         153771 ns       153765 ns         4543 RowInvRate=153.765ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/5                       1525405 ns      1525194 ns          450 RowInvRate=152.519ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/5                     17811960 ns     17811265 ns           40 RowInvRate=178.113ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/5                         209493 ns       209482 ns         3340 RowInvRate=209.482ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/5                       2069740 ns      2069618 ns          337 RowInvRate=206.962ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/5                     23765440 ns     23763385 ns           31 RowInvRate=237.634ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/5                         244783 ns       244762 ns         2862 RowInvRate=244.762ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/5                       2410932 ns      2410832 ns          288 RowInvRate=241.083ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/5                     27223073 ns     27220730 ns           26 RowInvRate=272.207ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/10                        115874 ns       115858 ns         6102 RowInvRate=115.858ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/10                      1116964 ns      1116887 ns          622 RowInvRate=111.689ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/10                    13695884 ns     13695275 ns           50 RowInvRate=136.953ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/10                        150234 ns       150202 ns         4672 RowInvRate=150.202ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/10                      1471199 ns      1471073 ns          467 RowInvRate=147.107ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/10                    17101831 ns     17100950 ns           40 RowInvRate=171.009ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/10                        178159 ns       178152 ns         3931 RowInvRate=178.152ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/10                      1763623 ns      1763524 ns          402 RowInvRate=176.352ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/10                    19876271 ns     19873872 ns           34 RowInvRate=198.739ns
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
            TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
            TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
            TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
            TypeDescriptor::from_logical_type(TYPE_ARRAY)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
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
            ctx->set_constant_columns({nullptr, starting_nodes_column, nodes_column, ending_nodes_column,
                                       excluding_nodes_column, excluding_all_nodes_column, any_nodes_column});
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
        ASSERT_TRUE(CelonisMatchActivitiesFunctions::celonis_match_activities(
                            ctx.get(), {variant_column, starting_nodes_column, nodes_column, ending_nodes_column,
                                        excluding_nodes_column, excluding_all_nodes_column, any_nodes_column})
                            .ok());
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
BENCHMARK(BM_MatchActivitiesNonConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantLargeMatchesConfig)
        ->ArgsProduct({{1000, 10000, 100000, 1000000}, {20}, {5000}, {3000}, {0}, {0}, {0}, {0}, {0}});
// NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {1, 5, 10}, {0}, {0}, {0}, {0}});
// ENDING nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {5, 10}, {0}, {0}, {0}});
// EXCLUDING_ALL nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {5, 10}, {0}});
// ANY_NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {0}, {5, 10}});

} // namespace starrocks

BENCHMARK_MAIN();
