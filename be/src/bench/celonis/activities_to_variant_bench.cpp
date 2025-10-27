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
2025-06-08T11:03:50+00:00
Running ./be/build_Release/src/bench/celonis/output/activities_to_variant_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.45, 4.88, 3.04
// Number of rows / Number of distinct activities / Variant length / NULL activity percentage
--------------------------------------------------------------------------------------------------
Benchmark                                        Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------
BM_ActivitiesToVariant/10000/20/20/0       1311791 ns      1311740 ns          537 RowInvRate=131.174ns
BM_ActivitiesToVariant/100000/20/20/0     21599895 ns     21598768 ns           34 RowInvRate=215.988ns
BM_ActivitiesToVariant/10000/100/20/0      1297012 ns      1297023 ns          535 RowInvRate=129.702ns
BM_ActivitiesToVariant/100000/100/20/0    22267631 ns     22267412 ns           31 RowInvRate=222.674ns
BM_ActivitiesToVariant/10000/1000/20/0     1343837 ns      1343842 ns          527 RowInvRate=134.384ns
BM_ActivitiesToVariant/100000/1000/20/0   23646703 ns     23642697 ns           29 RowInvRate=236.427ns
BM_ActivitiesToVariant/10000/20/40/0       2804384 ns      2804190 ns          259 RowInvRate=280.419ns
BM_ActivitiesToVariant/100000/20/40/0     43181892 ns     43182148 ns           15 RowInvRate=431.821ns
BM_ActivitiesToVariant/10000/100/40/0      2727385 ns      2727289 ns          243 RowInvRate=272.729ns
BM_ActivitiesToVariant/100000/100/40/0    41736233 ns     41736404 ns           17 RowInvRate=417.364ns
BM_ActivitiesToVariant/10000/1000/40/0     2797260 ns      2797077 ns          249 RowInvRate=279.708ns
BM_ActivitiesToVariant/100000/1000/40/0   42907029 ns     42906553 ns           16 RowInvRate=429.066ns
BM_ActivitiesToVariant/10000/20/20/5       1630374 ns      1630363 ns          416 RowInvRate=163.036ns
BM_ActivitiesToVariant/100000/20/20/5     26183525 ns     26182115 ns           26 RowInvRate=261.821ns
BM_ActivitiesToVariant/10000/100/20/5      1665294 ns      1665123 ns          431 RowInvRate=166.512ns
BM_ActivitiesToVariant/100000/100/20/5    26717973 ns     26717086 ns           26 RowInvRate=267.171ns
BM_ActivitiesToVariant/10000/1000/20/5     1694907 ns      1694857 ns          414 RowInvRate=169.486ns
BM_ActivitiesToVariant/100000/1000/20/5   27700125 ns     27695157 ns           25 RowInvRate=276.952ns
BM_ActivitiesToVariant/10000/20/40/5       3244239 ns      3243889 ns          213 RowInvRate=324.389ns
BM_ActivitiesToVariant/100000/20/40/5     46558015 ns     46555275 ns           15 RowInvRate=465.553ns
BM_ActivitiesToVariant/10000/100/40/5      3254239 ns      3254186 ns          208 RowInvRate=325.419ns
BM_ActivitiesToVariant/100000/100/40/5    47094508 ns     47092897 ns           14 RowInvRate=470.929ns
BM_ActivitiesToVariant/10000/1000/40/5     3343522 ns      3343575 ns          209 RowInvRate=334.357ns
BM_ActivitiesToVariant/100000/1000/40/5   48342908 ns     48342293 ns           15 RowInvRate=483.423ns
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
BENCHMARK(BM_ActivitiesToVariant)->ArgsProduct({{10000, 100000}, {20, 100, 1000}, {20, 40}, {0, 5}});

} // namespace starrocks

BENCHMARK_MAIN();
