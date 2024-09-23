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
2024-09-23T20:36:00+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 3599.97 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.00, 1.30, 2.43
----------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                  Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5                     1166005 ns      1165867 ns          601 RowInvRate=1.16587us
BM_MatchActivitiesNonConstantConfig/10000/20/20/5                   11604390 ns     11603537 ns           60 RowInvRate=1.16035us
BM_MatchActivitiesNonConstantConfig/100000/20/20/5                 133645470 ns    133637089 ns            5 RowInvRate=1.33637us
BM_MatchActivitiesNonConstantConfig/1000/20/40/5                     1112003 ns      1111643 ns          629 RowInvRate=1.11164us
BM_MatchActivitiesNonConstantConfig/10000/20/40/5                   11057622 ns     11056939 ns           64 RowInvRate=1.10569us
BM_MatchActivitiesNonConstantConfig/100000/20/40/5                 127435574 ns    127419560 ns            5 RowInvRate=1.2742us
BM_MatchActivitiesNonConstantConfig/1000/20/60/5                     1091478 ns      1091199 ns          642 RowInvRate=1091.2ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5                   10854067 ns     10853080 ns           65 RowInvRate=1085.31ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5                 124702094 ns    124688471 ns            6 RowInvRate=1.24688us
BM_MatchActivitiesNonConstantConfig/1000/20/20/10                    1817542 ns      1817340 ns          385 RowInvRate=1.81734us
BM_MatchActivitiesNonConstantConfig/10000/20/20/10                  18079852 ns     18077560 ns           39 RowInvRate=1.80776us
BM_MatchActivitiesNonConstantConfig/100000/20/20/10                198308974 ns    198282187 ns            4 RowInvRate=1.98282us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10                    1757381 ns      1756812 ns          399 RowInvRate=1.75681us
BM_MatchActivitiesNonConstantConfig/10000/20/40/10                  17417547 ns     17411405 ns           40 RowInvRate=1.74114us
BM_MatchActivitiesNonConstantConfig/100000/20/40/10                192156728 ns    192132620 ns            4 RowInvRate=1.92133us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10                    1716876 ns      1716438 ns          409 RowInvRate=1.71644us
BM_MatchActivitiesNonConstantConfig/10000/20/60/10                  17084635 ns     17075674 ns           41 RowInvRate=1.70757us
BM_MatchActivitiesNonConstantConfig/100000/20/60/10                188902485 ns    188870290 ns            4 RowInvRate=1.8887us
BM_MatchActivitiesConstantConfig/1000/20/20/5                         138958 ns       138882 ns         5024 RowInvRate=138.882ns
BM_MatchActivitiesConstantConfig/10000/20/20/5                       1406058 ns      1405904 ns          488 RowInvRate=140.59ns
BM_MatchActivitiesConstantConfig/100000/20/20/5                     29848575 ns     29844931 ns           23 RowInvRate=298.449ns
BM_MatchActivitiesConstantConfig/1000/20/40/5                          92585 ns        92551 ns         7594 RowInvRate=92.5507ns
BM_MatchActivitiesConstantConfig/10000/20/40/5                        917723 ns       917567 ns          773 RowInvRate=91.7567ns
BM_MatchActivitiesConstantConfig/100000/20/40/5                     25050892 ns     25047317 ns           27 RowInvRate=250.473ns
BM_MatchActivitiesConstantConfig/1000/20/60/5                          76650 ns        76645 ns         9078 RowInvRate=76.6446ns
BM_MatchActivitiesConstantConfig/10000/20/60/5                        759684 ns       759606 ns          962 RowInvRate=75.9606ns
BM_MatchActivitiesConstantConfig/100000/20/60/5                     23642733 ns     23640121 ns           29 RowInvRate=236.401ns
BM_MatchActivitiesConstantConfig/1000/20/20/10                        209224 ns       209162 ns         3359 RowInvRate=209.162ns
BM_MatchActivitiesConstantConfig/10000/20/20/10                      2103466 ns      2103234 ns          346 RowInvRate=210.323ns
BM_MatchActivitiesConstantConfig/100000/20/20/10                    35831964 ns     35824480 ns           19 RowInvRate=358.245ns
BM_MatchActivitiesConstantConfig/1000/20/40/10                        131073 ns       131059 ns         5219 RowInvRate=131.059ns
BM_MatchActivitiesConstantConfig/10000/20/40/10                      1321689 ns      1321497 ns          535 RowInvRate=132.15ns
BM_MatchActivitiesConstantConfig/100000/20/40/10                    28683960 ns     28680140 ns           24 RowInvRate=286.801ns
BM_MatchActivitiesConstantConfig/1000/20/60/10                        104207 ns       104208 ns         6688 RowInvRate=104.208ns
BM_MatchActivitiesConstantConfig/10000/20/60/10                      1020608 ns      1020465 ns          673 RowInvRate=102.047ns
BM_MatchActivitiesConstantConfig/100000/20/60/10                    26168236 ns     26159972 ns           27 RowInvRate=261.6ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000        486821 ns       486707 ns         1433 RowInvRate=486.707ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000      1871370 ns      1870894 ns          380 RowInvRate=187.089ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000    31855664 ns     31851125 ns           22 RowInvRate=318.511ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000  334182841 ns    334141508 ns            2 RowInvRate=334.142ns
*/

enum MatchType {
    CONSTANT,
    NON_CONSTANT,
};

static void do_bench(benchmark::State& state, MatchType match_type) {
    int num_rows = state.range(0);
    int variant_length = state.range(1);
    int num_values = state.range(2);
    int match_size = state.range(3);

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
                starting_nodes_column->append_datum(gen_rand_array(match_size));
                nodes_column->append_datum(gen_rand_array(0));
                ending_nodes_column->append_datum(gen_rand_array(match_size));
                excluding_nodes_column->append_datum(gen_rand_array(0));
                excluding_all_nodes_column->append_datum(gen_rand_array(0));
                any_nodes_column->append_datum(gen_rand_array(0));
                ctx->set_constant_columns(
                        {nullptr, starting_nodes_column, nodes_column, ending_nodes_column, excluding_nodes_column,
                         excluding_all_nodes_column, any_nodes_column});
                break;
            case NON_CONSTANT:
                for (int i = 0; i < num_rows; i++) {
                    starting_nodes_column->append_datum(gen_rand_array(match_size));
                    nodes_column->append_datum(gen_rand_array(0));
                    ending_nodes_column->append_datum(gen_rand_array(match_size));
                    excluding_nodes_column->append_datum(gen_rand_array(0));
                    excluding_all_nodes_column->append_datum(gen_rand_array(0));
                    any_nodes_column->append_datum(gen_rand_array(0));
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

// Args: Number of rows/ Length of each variant / Number of possible values / Size of match list
BENCHMARK(BM_MatchActivitiesNonConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}});
BENCHMARK(BM_MatchActivitiesConstantConfig)->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}});
BENCHMARK(BM_MatchActivitiesConstantLargeMatchesConfig)->ArgsProduct({{1000, 10000, 100000, 1000000}, {20}, {5000}, {3000}});

} // namespace starrocks

BENCHMARK_MAIN();

