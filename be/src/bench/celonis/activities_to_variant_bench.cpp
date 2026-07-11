#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/array_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-11-23T16:14:40+00:00
Running ./be/build_Release/src/bench/celonis/output/activities_to_variant_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 15.42, 6.42, 3.56
// Number of rows / Number of distinct activities / Variant length / NULL activity percentage
--------------------------------------------------------------------------------------------------
Benchmark                                        Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------
BM_ActivitiesToVariant/10000/20/20/0       1238494 ns      1238396 ns          569 RowInvRate=123.84ns
BM_ActivitiesToVariant/100000/20/20/0     19846280 ns     19843695 ns           39 RowInvRate=198.437ns
BM_ActivitiesToVariant/10000/100/20/0      1339009 ns      1338765 ns          506 RowInvRate=133.877ns
BM_ActivitiesToVariant/100000/100/20/0    19043008 ns     19042107 ns           37 RowInvRate=190.421ns
BM_ActivitiesToVariant/10000/1000/20/0     1319194 ns      1319271 ns          511 RowInvRate=131.927ns
BM_ActivitiesToVariant/100000/1000/20/0   19481310 ns     19480114 ns           35 RowInvRate=194.801ns
BM_ActivitiesToVariant/10000/20/40/0       2471885 ns      2471653 ns          284 RowInvRate=247.165ns
BM_ActivitiesToVariant/100000/20/40/0     36757998 ns     36753848 ns           19 RowInvRate=367.538ns
BM_ActivitiesToVariant/10000/100/40/0      2476460 ns      2476281 ns          267 RowInvRate=247.628ns
BM_ActivitiesToVariant/100000/100/40/0    38338823 ns     38333732 ns           18 RowInvRate=383.337ns
BM_ActivitiesToVariant/10000/1000/40/0     2637299 ns      2637173 ns          272 RowInvRate=263.717ns
BM_ActivitiesToVariant/100000/1000/40/0   39145093 ns     39143386 ns           18 RowInvRate=391.434ns
BM_ActivitiesToVariant/10000/20/20/5       1392610 ns      1392654 ns          494 RowInvRate=139.265ns
BM_ActivitiesToVariant/100000/20/20/5     20511512 ns     20510612 ns           35 RowInvRate=205.106ns
BM_ActivitiesToVariant/10000/100/20/5      1344688 ns      1344697 ns          493 RowInvRate=134.47ns
BM_ActivitiesToVariant/100000/100/20/5    20386803 ns     20383999 ns           34 RowInvRate=203.84ns
BM_ActivitiesToVariant/10000/1000/20/5     1400845 ns      1400719 ns          498 RowInvRate=140.072ns
BM_ActivitiesToVariant/100000/1000/20/5   20841824 ns     20841868 ns           34 RowInvRate=208.419ns
BM_ActivitiesToVariant/10000/20/40/5       2640883 ns      2640666 ns          267 RowInvRate=264.067ns
BM_ActivitiesToVariant/100000/20/40/5     38635763 ns     38631392 ns           18 RowInvRate=386.314ns
BM_ActivitiesToVariant/10000/100/40/5      2648670 ns      2648038 ns          263 RowInvRate=264.804ns
BM_ActivitiesToVariant/100000/100/40/5    38882396 ns     38880367 ns           18 RowInvRate=388.804ns
BM_ActivitiesToVariant/10000/1000/40/5     2815016 ns      2814638 ns          250 RowInvRate=281.464ns
BM_ActivitiesToVariant/100000/1000/40/5   40569583 ns     40565154 ns           17 RowInvRate=405.652ns
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

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_ARRAY)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_VARCHAR);
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
