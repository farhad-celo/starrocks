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
2024-09-24T15:33:02+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 1988.97 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.43, 4.78, 3.52
Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
--------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                            Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5/0/0/0/0/0                      698545 ns       698491 ns         1003 RowInvRate=698.491ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/5/0/0/0/0/0                    6868984 ns      6868031 ns           98 RowInvRate=686.803ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/5/0/0/0/0/0                  85478207 ns     85468125 ns            8 RowInvRate=854.681ns
BM_MatchActivitiesNonConstantConfig/1000/20/40/5/0/0/0/0/0                      645749 ns       645705 ns         1083 RowInvRate=645.705ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/5/0/0/0/0/0                    6397032 ns      6396511 ns          108 RowInvRate=639.651ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/5/0/0/0/0/0                  81010715 ns     81001905 ns            9 RowInvRate=810.019ns
BM_MatchActivitiesNonConstantConfig/1000/20/60/5/0/0/0/0/0                      624581 ns       624510 ns         1123 RowInvRate=624.51ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5/0/0/0/0/0                    6192154 ns      6191552 ns          112 RowInvRate=619.155ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5/0/0/0/0/0                  78360393 ns     78350641 ns            9 RowInvRate=783.506ns
BM_MatchActivitiesNonConstantConfig/1000/20/20/10/0/0/0/0/0                    1069997 ns      1069698 ns          655 RowInvRate=1069.7ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/10/0/0/0/0/0                  10626420 ns     10624498 ns           66 RowInvRate=1062.45ns
BM_MatchActivitiesNonConstantConfig/100000/20/20/10/0/0/0/0/0                123331130 ns    123321655 ns            6 RowInvRate=1.23322us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10/0/0/0/0/0                    1000058 ns       999927 ns          702 RowInvRate=999.927ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/10/0/0/0/0/0                   9898479 ns      9895653 ns           71 RowInvRate=989.565ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/10/0/0/0/0/0                116433210 ns    116423076 ns            6 RowInvRate=1.16423us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10/0/0/0/0/0                     965433 ns       965299 ns          727 RowInvRate=965.299ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/10/0/0/0/0/0                   9639720 ns      9637995 ns           73 RowInvRate=963.799ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/10/0/0/0/0/0                112592180 ns    112580107 ns            6 RowInvRate=1.1258us
BM_MatchActivitiesConstantConfig/1000/20/20/5/0/0/0/0/0                         135986 ns       135969 ns         5132 RowInvRate=135.969ns
BM_MatchActivitiesConstantConfig/10000/20/20/5/0/0/0/0/0                       1380329 ns      1380214 ns          526 RowInvRate=138.021ns
BM_MatchActivitiesConstantConfig/100000/20/20/5/0/0/0/0/0                     29699725 ns     29693334 ns           23 RowInvRate=296.933ns
BM_MatchActivitiesConstantConfig/1000/20/40/5/0/0/0/0/0                          93290 ns        93197 ns         7513 RowInvRate=93.1973ns
BM_MatchActivitiesConstantConfig/10000/20/40/5/0/0/0/0/0                        897453 ns       897441 ns          782 RowInvRate=89.7441ns
BM_MatchActivitiesConstantConfig/100000/20/40/5/0/0/0/0/0                     25360116 ns     25358356 ns           28 RowInvRate=253.584ns
BM_MatchActivitiesConstantConfig/1000/20/60/5/0/0/0/0/0                          76209 ns        76177 ns         9240 RowInvRate=76.1771ns
BM_MatchActivitiesConstantConfig/10000/20/60/5/0/0/0/0/0                        758069 ns       758049 ns          985 RowInvRate=75.8049ns
BM_MatchActivitiesConstantConfig/100000/20/60/5/0/0/0/0/0                     23978903 ns     23973931 ns           29 RowInvRate=239.739ns
BM_MatchActivitiesConstantConfig/1000/20/20/10/0/0/0/0/0                        201554 ns       201545 ns         3456 RowInvRate=201.545ns
BM_MatchActivitiesConstantConfig/10000/20/20/10/0/0/0/0/0                      2044388 ns      2044136 ns          349 RowInvRate=204.414ns
BM_MatchActivitiesConstantConfig/100000/20/20/10/0/0/0/0/0                    36358071 ns     36354091 ns           19 RowInvRate=363.541ns
BM_MatchActivitiesConstantConfig/1000/20/40/10/0/0/0/0/0                        128561 ns       128542 ns         5486 RowInvRate=128.542ns
BM_MatchActivitiesConstantConfig/10000/20/40/10/0/0/0/0/0                      1271718 ns      1271601 ns          564 RowInvRate=127.16ns
BM_MatchActivitiesConstantConfig/100000/20/40/10/0/0/0/0/0                    28933771 ns     28931937 ns           24 RowInvRate=289.319ns
BM_MatchActivitiesConstantConfig/1000/20/60/10/0/0/0/0/0                        103323 ns       103283 ns         6739 RowInvRate=103.283ns
BM_MatchActivitiesConstantConfig/10000/20/60/10/0/0/0/0/0                      1003824 ns      1003775 ns          690 RowInvRate=100.378ns
BM_MatchActivitiesConstantConfig/100000/20/60/10/0/0/0/0/0                    26394653 ns     26392678 ns           26 RowInvRate=263.927ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000/0/0/0/0/0        311581 ns       311536 ns         2252 RowInvRate=311.536ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000/0/0/0/0/0      1570057 ns      1569725 ns          456 RowInvRate=156.973ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000/0/0/0/0/0    30892244 ns     30888240 ns           23 RowInvRate=308.882ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000/0/0/0/0/0  331185532 ns    331117660 ns            2 RowInvRate=331.118ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/5/0                         621185 ns       620847 ns         1130 RowInvRate=620.847ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/5/0                       6157739 ns      6154808 ns          111 RowInvRate=615.481ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/5/0                     77779629 ns     77768804 ns            9 RowInvRate=777.688ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/5/0                         505618 ns       505547 ns         1380 RowInvRate=505.547ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/5/0                       5024776 ns      5024493 ns          100 RowInvRate=502.449ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/5/0                     66438041 ns     66431352 ns           11 RowInvRate=664.314ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/5/0                         453652 ns       453404 ns         1544 RowInvRate=453.404ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/5/0                       4554538 ns      4554125 ns          154 RowInvRate=455.412ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/5/0                     60876132 ns     60872309 ns           11 RowInvRate=608.723ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/10/0                        769117 ns       769039 ns          911 RowInvRate=769.039ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/10/0                      7748240 ns      7746995 ns           91 RowInvRate=774.7ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/10/0                    91843963 ns     91826105 ns            8 RowInvRate=918.261ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/10/0                        616553 ns       616462 ns         1131 RowInvRate=616.462ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/10/0                      6144522 ns      6142586 ns          116 RowInvRate=614.259ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/10/0                    77694740 ns     77690021 ns            9 RowInvRate=776.9ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/10/0                        536378 ns       536252 ns         1302 RowInvRate=536.252ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/10/0                      5336316 ns      5335706 ns          123 RowInvRate=533.571ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/10/0                    69641522 ns     69628512 ns           10 RowInvRate=696.285ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/5                         371556 ns       371408 ns         1883 RowInvRate=371.408ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/5                       3753745 ns      3753355 ns          186 RowInvRate=375.336ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/5                     53195115 ns     53188899 ns           13 RowInvRate=531.889ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/5                         348432 ns       348396 ns         2010 RowInvRate=348.396ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/5                       3500849 ns      3500293 ns          199 RowInvRate=350.029ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/5                     50765999 ns     50761234 ns           13 RowInvRate=507.612ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/5                         340758 ns       340706 ns         2057 RowInvRate=340.706ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/5                       3422129 ns      3421430 ns          205 RowInvRate=342.143ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/5                     50380170 ns     50371689 ns           14 RowInvRate=503.717ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/0/10                        368922 ns       368849 ns         1898 RowInvRate=368.849ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/0/10                      3697975 ns      3697678 ns          188 RowInvRate=369.768ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/0/10                    52693516 ns     52688161 ns           13 RowInvRate=526.882ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/0/10                        343060 ns       343028 ns         2042 RowInvRate=343.028ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/0/10                      3440577 ns      3439910 ns          202 RowInvRate=343.991ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/0/10                    50665476 ns     50657981 ns           13 RowInvRate=506.58ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/0/10                        331936 ns       331820 ns         2112 RowInvRate=331.82ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/0/10                      3341608 ns      3341044 ns          208 RowInvRate=334.104ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/0/10                    49632168 ns     49625958 ns           14 RowInvRate=496.26ns
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
// EXCLUDING_ALL nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {5, 10}, {0}});
// ANY_NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {0}, {5, 10}});


} // namespace starrocks

BENCHMARK_MAIN();

