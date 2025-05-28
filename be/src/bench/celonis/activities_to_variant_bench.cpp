#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-05-27T21:49:53+00:00
Running ./be/build_Release/src/bench/celonis/output/activities_to_variant_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.71, 2.66, 4.15
// Number of rows / Number of distinct activities / Variant length / NULL activity percentage
--------------------------------------------------------------------------------------------------
Benchmark                                        Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------
BM_ActivitiesToVariant/10000/20/20/0       3478853 ns      3478769 ns          202 RowInvRate=347.877ns
BM_ActivitiesToVariant/100000/20/20/0     41224443 ns     41222845 ns           17 RowInvRate=412.228ns
BM_ActivitiesToVariant/10000/100/20/0      3496604 ns      3496134 ns          201 RowInvRate=349.613ns
BM_ActivitiesToVariant/100000/100/20/0    41821204 ns     41815016 ns           16 RowInvRate=418.15ns
BM_ActivitiesToVariant/10000/1000/20/0     3486633 ns      3486565 ns          199 RowInvRate=348.656ns
BM_ActivitiesToVariant/100000/1000/20/0   42613029 ns     42611517 ns           16 RowInvRate=426.115ns
BM_ActivitiesToVariant/10000/20/40/0       6941474 ns      6941485 ns          100 RowInvRate=694.149ns
BM_ActivitiesToVariant/100000/20/40/0     80064204 ns     80058940 ns            9 RowInvRate=800.589ns
BM_ActivitiesToVariant/10000/100/40/0      6980492 ns      6980199 ns          101 RowInvRate=698.02ns
BM_ActivitiesToVariant/100000/100/40/0    82226399 ns     82225805 ns            9 RowInvRate=822.258ns
BM_ActivitiesToVariant/10000/1000/40/0     7248131 ns      7248107 ns           97 RowInvRate=724.811ns
BM_ActivitiesToVariant/100000/1000/40/0   82863360 ns     82859089 ns            9 RowInvRate=828.591ns
BM_ActivitiesToVariant/10000/20/20/5       3663590 ns      3663402 ns          193 RowInvRate=366.34ns
BM_ActivitiesToVariant/100000/20/20/5     45267253 ns     45265662 ns           16 RowInvRate=452.657ns
BM_ActivitiesToVariant/10000/100/20/5      3686076 ns      3685916 ns          187 RowInvRate=368.592ns
BM_ActivitiesToVariant/100000/100/20/5    44621359 ns     44620151 ns           16 RowInvRate=446.202ns
BM_ActivitiesToVariant/10000/1000/20/5     3654010 ns      3653985 ns          187 RowInvRate=365.399ns
BM_ActivitiesToVariant/100000/1000/20/5   45394936 ns     45392864 ns           15 RowInvRate=453.929ns
BM_ActivitiesToVariant/10000/20/40/5       7201933 ns      7201558 ns           96 RowInvRate=720.156ns
BM_ActivitiesToVariant/100000/20/40/5     83504551 ns     83500203 ns            9 RowInvRate=835.002ns
BM_ActivitiesToVariant/10000/100/40/5      7335339 ns      7335270 ns           94 RowInvRate=733.527ns
BM_ActivitiesToVariant/100000/100/40/5    84998550 ns     84994325 ns            8 RowInvRate=849.943ns
BM_ActivitiesToVariant/10000/1000/40/5     7367857 ns      7367736 ns           95 RowInvRate=736.774ns
BM_ActivitiesToVariant/100000/1000/40/5   85931599 ns     85925228 ns            8 RowInvRate=859.252ns
*/

static void do_bench(benchmark::State& state) {
    const int num_rows = state.range(0);
    const int num_distinct_activities = state.range(1);
    const int variant_length = state.range(2);
    double null_probability = state.range(3) / 100.0;

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_distinct_activities - 1);
    std::bernoulli_distribution null_dist(null_probability);

    std::vector<std::string> values;
    values.reserve(num_distinct_activities);
    for (int i = 0; i < num_distinct_activities; i++) {
        values.push_back("Activity" + std::to_string(i));
    }

    auto gen_rand_element = [&]() { return Slice(values[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            bool is_null = null_dist(rng);
            if (is_null) {
                array.emplace_back(kNullDatum);
            } else {
                array.emplace_back(gen_rand_element());
            }
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
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
        ctx->set_constant_columns({nullptr});

        state.ResumeTiming();
        ASSERT_TRUE(CelonisArrayFunctions::activities_to_variant(ctx.get(), {variant_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ActivitiesToVariant(benchmark::State& state) {
    do_bench(state);
}

// Number of rows / Number of distinct activities / Variant length / NULL activity percentage
BENCHMARK(BM_ActivitiesToVariant) ->ArgsProduct({{10000, 100000}, {20, 100, 1000}, {20, 40}, {0, 5}});

} // namespace starrocks

BENCHMARK_MAIN();
