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
2024-09-09T18:49:23+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 3605.42 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.51, 1.89, 4.12
----------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                  Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5                     1582567 ns      1582382 ns          441 RowInvRate=1.58238us
BM_MatchActivitiesNonConstantConfig/10000/20/20/5                   15813524 ns     15811854 ns           44 RowInvRate=1.58119us
BM_MatchActivitiesNonConstantConfig/100000/20/20/5                 176809810 ns    176794206 ns            4 RowInvRate=1.76794us
BM_MatchActivitiesNonConstantConfig/1000/20/40/5                     1545212 ns      1545070 ns          451 RowInvRate=1.54507us
BM_MatchActivitiesNonConstantConfig/10000/20/40/5                   15372797 ns     15371649 ns           46 RowInvRate=1.53716us
BM_MatchActivitiesNonConstantConfig/100000/20/40/5                 175657435 ns    175634840 ns            4 RowInvRate=1.75635us
BM_MatchActivitiesNonConstantConfig/1000/20/60/5                     1526121 ns      1525928 ns          458 RowInvRate=1.52593us
BM_MatchActivitiesNonConstantConfig/10000/20/60/5                   15204453 ns     15203309 ns           46 RowInvRate=1.52033us
BM_MatchActivitiesNonConstantConfig/100000/20/60/5                 171269474 ns    171254334 ns            4 RowInvRate=1.71254us
BM_MatchActivitiesNonConstantConfig/1000/20/20/10                    2210420 ns      2210223 ns          317 RowInvRate=2.21022us
BM_MatchActivitiesNonConstantConfig/10000/20/20/10                  21901403 ns     21899743 ns           32 RowInvRate=2.18997us
BM_MatchActivitiesNonConstantConfig/100000/20/20/10                239156311 ns    239123873 ns            3 RowInvRate=2.39124us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10                    2190739 ns      2190607 ns          319 RowInvRate=2.19061us
BM_MatchActivitiesNonConstantConfig/10000/20/40/10                  21819623 ns     21817143 ns           32 RowInvRate=2.18171us
BM_MatchActivitiesNonConstantConfig/100000/20/40/10                238596180 ns    238570716 ns            3 RowInvRate=2.38571us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10                    2164436 ns      2164160 ns          323 RowInvRate=2.16416us
BM_MatchActivitiesNonConstantConfig/10000/20/60/10                  21578390 ns     21576176 ns           32 RowInvRate=2.15762us
BM_MatchActivitiesNonConstantConfig/100000/20/60/10                235522739 ns    235504488 ns            3 RowInvRate=2.35504us
BM_MatchActivitiesConstantConfig/1000/20/20/5                         544421 ns       544342 ns         1286 RowInvRate=544.342ns
BM_MatchActivitiesConstantConfig/10000/20/20/5                       5410699 ns      5410035 ns          130 RowInvRate=541.004ns
BM_MatchActivitiesConstantConfig/100000/20/20/5                     72205512 ns     72198101 ns           10 RowInvRate=721.981ns
BM_MatchActivitiesConstantConfig/1000/20/40/5                         510866 ns       510779 ns         1371 RowInvRate=510.779ns
BM_MatchActivitiesConstantConfig/10000/20/40/5                       5104278 ns      5103444 ns          138 RowInvRate=510.344ns
BM_MatchActivitiesConstantConfig/100000/20/40/5                     68675536 ns     68651567 ns           10 RowInvRate=686.516ns
BM_MatchActivitiesConstantConfig/1000/20/60/5                         495275 ns       495240 ns         1414 RowInvRate=495.24ns
BM_MatchActivitiesConstantConfig/10000/20/60/5                       4944456 ns      4944021 ns          139 RowInvRate=494.402ns
BM_MatchActivitiesConstantConfig/100000/20/60/5                     67075896 ns     67066373 ns           10 RowInvRate=670.664ns
BM_MatchActivitiesConstantConfig/1000/20/20/10                        546693 ns       546612 ns         1279 RowInvRate=546.612ns
BM_MatchActivitiesConstantConfig/10000/20/20/10                      5421315 ns      5420749 ns          129 RowInvRate=542.075ns
BM_MatchActivitiesConstantConfig/100000/20/20/10                    72323588 ns     72319122 ns           10 RowInvRate=723.191ns
BM_MatchActivitiesConstantConfig/1000/20/40/10                        511167 ns       511124 ns         1366 RowInvRate=511.124ns
BM_MatchActivitiesConstantConfig/10000/20/40/10                      5078962 ns      5078271 ns          134 RowInvRate=507.827ns
BM_MatchActivitiesConstantConfig/100000/20/40/10                    68836830 ns     68826409 ns           10 RowInvRate=688.264ns
BM_MatchActivitiesConstantConfig/1000/20/60/10                        496078 ns       496050 ns         1410 RowInvRate=496.05ns
BM_MatchActivitiesConstantConfig/10000/20/60/10                      4985216 ns      4984565 ns          135 RowInvRate=498.456ns
BM_MatchActivitiesConstantConfig/100000/20/60/10                    67411895 ns     67406459 ns           10 RowInvRate=674.065ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000        729105 ns       728897 ns          965 RowInvRate=728.897ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000      4274980 ns      4274112 ns          164 RowInvRate=427.411ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000    57926742 ns     57915350 ns           12 RowInvRate=579.154ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000  564889141 ns    564746306 ns            1 RowInvRate=564.746ns
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

