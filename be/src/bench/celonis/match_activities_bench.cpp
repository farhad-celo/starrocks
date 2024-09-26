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
2024-09-25T15:16:40+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.37, 1.84, 1.67
// Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
--------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5/0/0/0/0/0                      779899 ns       779796 ns          901 RowInvRate=779.796ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/5/0/0/0/0/0                    7725200 ns      7724517 ns           88 RowInvRate=772.452ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/5/0/0/0/0/0                  93531618 ns     93525635 ns            7 RowInvRate=935.256ns
BM_MatchActivitiesNonConstantConfig/1000/20/40/5/0/0/0/0/0                      736121 ns       735967 ns          959 RowInvRate=735.967ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/5/0/0/0/0/0                    7275547 ns      7274303 ns           97 RowInvRate=727.43ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/5/0/0/0/0/0                  89196993 ns     89187518 ns            8 RowInvRate=891.875ns
BM_MatchActivitiesNonConstantConfig/1000/20/60/5/0/0/0/0/0                      719353 ns       719276 ns          968 RowInvRate=719.276ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5/0/0/0/0/0                    7073946 ns      7073401 ns           99 RowInvRate=707.34ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5/0/0/0/0/0                  87785826 ns     87774573 ns            8 RowInvRate=877.746ns
BM_MatchActivitiesNonConstantConfig/1000/20/20/10/0/0/0/0/0                    1143196 ns      1142795 ns          614 RowInvRate=1.14279us
BM_MatchActivitiesNonConstantConfig/10000/20/20/10/0/0/0/0/0                  11289640 ns     11288630 ns           61 RowInvRate=1.12886us
BM_MatchActivitiesNonConstantConfig/100000/20/20/10/0/0/0/0/0                129979836 ns    129966386 ns            5 RowInvRate=1.29966us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10/0/0/0/0/0                    1085521 ns      1085370 ns          647 RowInvRate=1085.37ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/10/0/0/0/0/0                  10688673 ns     10683080 ns           65 RowInvRate=1068.31ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/10/0/0/0/0/0                125165578 ns    125153076 ns            6 RowInvRate=1.25153us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10/0/0/0/0/0                    1052962 ns      1052823 ns          666 RowInvRate=1052.82ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/10/0/0/0/0/0                  10424342 ns     10422389 ns           67 RowInvRate=1042.24ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/10/0/0/0/0/0                121374428 ns    121355621 ns            6 RowInvRate=1.21356us
BM_MatchActivitiesConstantConfig/1000/20/20/5/0/0/0/0/0                         139979 ns       139924 ns         5041 RowInvRate=139.924ns
BM_MatchActivitiesConstantConfig/10000/20/20/5/0/0/0/0/0                       1390967 ns      1390853 ns          492 RowInvRate=139.085ns
BM_MatchActivitiesConstantConfig/100000/20/20/5/0/0/0/0/0                     30063486 ns     30061092 ns           23 RowInvRate=300.611ns
BM_MatchActivitiesConstantConfig/1000/20/40/5/0/0/0/0/0                          94614 ns        94539 ns         7468 RowInvRate=94.5388ns
BM_MatchActivitiesConstantConfig/10000/20/40/5/0/0/0/0/0                        919118 ns       918969 ns          771 RowInvRate=91.8969ns
BM_MatchActivitiesConstantConfig/100000/20/40/5/0/0/0/0/0                     25811880 ns     25810231 ns           27 RowInvRate=258.102ns
BM_MatchActivitiesConstantConfig/1000/20/60/5/0/0/0/0/0                          79013 ns        78911 ns         9024 RowInvRate=78.9114ns
BM_MatchActivitiesConstantConfig/10000/20/60/5/0/0/0/0/0                        771378 ns       771339 ns          904 RowInvRate=77.1339ns
BM_MatchActivitiesConstantConfig/100000/20/60/5/0/0/0/0/0                     24409457 ns     24406678 ns           29 RowInvRate=244.067ns
BM_MatchActivitiesConstantConfig/1000/20/20/10/0/0/0/0/0                        209525 ns       209492 ns         3345 RowInvRate=209.492ns
BM_MatchActivitiesConstantConfig/10000/20/20/10/0/0/0/0/0                      2096009 ns      2095587 ns          330 RowInvRate=209.559ns
BM_MatchActivitiesConstantConfig/100000/20/20/10/0/0/0/0/0                    36389744 ns     36386704 ns           19 RowInvRate=363.867ns
BM_MatchActivitiesConstantConfig/1000/20/40/10/0/0/0/0/0                        132439 ns       132394 ns         5318 RowInvRate=132.394ns
BM_MatchActivitiesConstantConfig/10000/20/40/10/0/0/0/0/0                      1318955 ns      1318696 ns          529 RowInvRate=131.87ns
BM_MatchActivitiesConstantConfig/100000/20/40/10/0/0/0/0/0                    29428042 ns     29422349 ns           23 RowInvRate=294.223ns
BM_MatchActivitiesConstantConfig/1000/20/60/10/0/0/0/0/0                        105222 ns       105132 ns         6733 RowInvRate=105.132ns
BM_MatchActivitiesConstantConfig/10000/20/60/10/0/0/0/0/0                      1043248 ns      1043150 ns          670 RowInvRate=104.315ns
BM_MatchActivitiesConstantConfig/100000/20/60/10/0/0/0/0/0                    26923740 ns     26920310 ns           26 RowInvRate=269.203ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000/0/0/0/0/0        319774 ns       319693 ns         2192 RowInvRate=319.693ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000/0/0/0/0/0      1696693 ns      1696524 ns          419 RowInvRate=169.652ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000/0/0/0/0/0    32188890 ns     32180257 ns           22 RowInvRate=321.803ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000/0/0/0/0/0  346122178 ns    346090617 ns            2 RowInvRate=346.091ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/5/0/0/0/0                         542612 ns       542352 ns         1293 RowInvRate=542.352ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/5/0/0/0/0                       5414021 ns      5411907 ns          130 RowInvRate=541.191ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/5/0/0/0/0                     70916506 ns     70904595 ns           10 RowInvRate=709.046ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/5/0/0/0/0                         441253 ns       441022 ns         1585 RowInvRate=441.022ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/5/0/0/0/0                       4434024 ns      4433542 ns          159 RowInvRate=443.354ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/5/0/0/0/0                     60681131 ns     60674294 ns           11 RowInvRate=606.743ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/5/0/0/0/0                         399011 ns       398961 ns         1747 RowInvRate=398.961ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/5/0/0/0/0                       3992816 ns      3991028 ns          176 RowInvRate=399.103ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/5/0/0/0/0                     56896572 ns     56890837 ns           12 RowInvRate=568.908ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/10/0/0/0/0                        575555 ns       575377 ns         1213 RowInvRate=575.377ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/10/0/0/0/0                      5759909 ns      5759439 ns          123 RowInvRate=575.944ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/10/0/0/0/0                    74119342 ns     74112895 ns            9 RowInvRate=741.129ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/10/0/0/0/0                        416667 ns       416576 ns         1678 RowInvRate=416.576ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/10/0/0/0/0                      4156516 ns      4155425 ns          168 RowInvRate=415.543ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/10/0/0/0/0                    58071551 ns     58066059 ns           12 RowInvRate=580.661ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/10/0/0/0/0                        347768 ns       347674 ns         2017 RowInvRate=347.674ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/10/0/0/0/0                      3463815 ns      3463302 ns          200 RowInvRate=346.33ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/10/0/0/0/0                    50722045 ns     50717529 ns           13 RowInvRate=507.175ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/5/0/0/0                         139604 ns       139569 ns         5006 RowInvRate=139.569ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/5/0/0/0                       1423719 ns      1423589 ns          516 RowInvRate=142.359ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/5/0/0/0                     31076639 ns     31069928 ns           22 RowInvRate=310.699ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/5/0/0/0                          92551 ns        92469 ns         7539 RowInvRate=92.4687ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/5/0/0/0                        912222 ns       912172 ns          760 RowInvRate=91.2172ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/5/0/0/0                     26341644 ns     26338200 ns           27 RowInvRate=263.382ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/5/0/0/0                          75749 ns        75708 ns         9180 RowInvRate=75.7075ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/5/0/0/0                        759983 ns       759916 ns          880 RowInvRate=75.9916ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/5/0/0/0                     24646039 ns     24643354 ns           28 RowInvRate=246.434ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/10/0/0/0                        209329 ns       209283 ns         3347 RowInvRate=209.283ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/10/0/0/0                      2122473 ns      2122115 ns          338 RowInvRate=212.212ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/10/0/0/0                    37501802 ns     37495670 ns           19 RowInvRate=374.957ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/10/0/0/0                        131766 ns       131694 ns         5303 RowInvRate=131.694ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/10/0/0/0                      1318801 ns      1318483 ns          544 RowInvRate=131.848ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/10/0/0/0                    30683233 ns     30679782 ns           23 RowInvRate=306.798ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/10/0/0/0                        103021 ns       102963 ns         6800 RowInvRate=102.963ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/10/0/0/0                      1041839 ns      1041673 ns          660 RowInvRate=104.167ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/10/0/0/0                    27456874 ns     27454335 ns           25 RowInvRate=274.543ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/5/0                         587957 ns       587770 ns         1186 RowInvRate=587.77ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/5/0                       5888051 ns      5884842 ns          116 RowInvRate=588.484ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/5/0                     75290808 ns     75281089 ns           10 RowInvRate=752.811ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/5/0                         502011 ns       501945 ns         1390 RowInvRate=501.945ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/5/0                       5038486 ns      5037099 ns          136 RowInvRate=503.71ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/5/0                     66607724 ns     66599221 ns           11 RowInvRate=665.992ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/5/0                         463637 ns       463540 ns         1513 RowInvRate=463.54ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/5/0                       4666344 ns      4665670 ns          150 RowInvRate=466.567ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/5/0                     62685865 ns     62671736 ns           11 RowInvRate=626.717ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/10/0                        686998 ns       686904 ns         1020 RowInvRate=686.904ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/10/0                      6886576 ns      6885590 ns          100 RowInvRate=688.559ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/10/0                    84202608 ns     84195100 ns            8 RowInvRate=841.951ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/10/0                        571992 ns       571860 ns         1224 RowInvRate=571.86ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/10/0                      5709405 ns      5708763 ns          115 RowInvRate=570.876ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/10/0                    73658839 ns     73651174 ns           10 RowInvRate=736.512ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/10/0                        513595 ns       513469 ns         1364 RowInvRate=513.469ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/10/0                      5114550 ns      5111761 ns          134 RowInvRate=511.176ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/10/0                    68255961 ns     68242553 ns           10 RowInvRate=682.426ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/5                         396283 ns       396125 ns         1767 RowInvRate=396.125ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/5                       3996631 ns      3996068 ns          175 RowInvRate=399.607ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/5                     56233958 ns     56224696 ns           12 RowInvRate=562.247ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/5                         378431 ns       378177 ns         1850 RowInvRate=378.177ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/5                       3768244 ns      3767501 ns          184 RowInvRate=376.75ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/5                     54146889 ns     54137381 ns           13 RowInvRate=541.374ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/5                         370465 ns       370396 ns         1893 RowInvRate=370.396ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/5                       3705675 ns      3705169 ns          189 RowInvRate=370.517ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/5                     53498910 ns     53490225 ns           13 RowInvRate=534.902ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/10                        391922 ns       391703 ns         1789 RowInvRate=391.703ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/10                      3931520 ns      3930921 ns          176 RowInvRate=393.092ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/10                    55034577 ns     55026208 ns           12 RowInvRate=550.262ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/10                        369493 ns       369329 ns         1897 RowInvRate=369.329ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/10                      3704901 ns      3704515 ns          189 RowInvRate=370.451ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/10                    53097826 ns     53089971 ns           13 RowInvRate=530.9ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/10                        361691 ns       361601 ns         1938 RowInvRate=361.601ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/10                      3622940 ns      3622447 ns          193 RowInvRate=362.245ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/10                    52430170 ns     52420777 ns           13 RowInvRate=524.208ns
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

