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
2025-06-01T15:52:29+00:00
Running ./be/build_Release/src/bench/celonis/output/activities_to_variant_bench
Run on (32 X 3240.68 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.60, 2.59, 1.92
// Number of rows / Number of distinct activities / Variant length / NULL activity percentage
--------------------------------------------------------------------------------------------------
Benchmark                                        Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------
BM_ActivitiesToVariant/10000/20/20/0       1697471 ns      1697445 ns          407 RowInvRate=169.744ns
BM_ActivitiesToVariant/100000/20/20/0     24221411 ns     24220339 ns           30 RowInvRate=242.203ns
BM_ActivitiesToVariant/10000/100/20/0      1731846 ns      1731817 ns          411 RowInvRate=173.182ns
BM_ActivitiesToVariant/100000/100/20/0    24267503 ns     24264349 ns           29 RowInvRate=242.643ns
BM_ActivitiesToVariant/10000/1000/20/0     1739653 ns      1739684 ns          398 RowInvRate=173.968ns
BM_ActivitiesToVariant/100000/1000/20/0   25444983 ns     25441910 ns           28 RowInvRate=254.419ns
BM_ActivitiesToVariant/10000/20/40/0       3427317 ns      3427210 ns          200 RowInvRate=342.721ns
BM_ActivitiesToVariant/100000/20/40/0     46621153 ns     46619922 ns           15 RowInvRate=466.199ns
BM_ActivitiesToVariant/10000/100/40/0      3395975 ns      3396030 ns          198 RowInvRate=339.603ns
BM_ActivitiesToVariant/100000/100/40/0    46597000 ns     46587662 ns           15 RowInvRate=465.877ns
BM_ActivitiesToVariant/10000/1000/40/0     3631513 ns      3630949 ns          196 RowInvRate=363.095ns
BM_ActivitiesToVariant/100000/1000/40/0   48677960 ns     48677070 ns           15 RowInvRate=486.771ns
BM_ActivitiesToVariant/10000/20/20/5       1963081 ns      1962871 ns          360 RowInvRate=196.287ns
BM_ActivitiesToVariant/100000/20/20/5     26164179 ns     26161668 ns           26 RowInvRate=261.617ns
BM_ActivitiesToVariant/10000/100/20/5      1987159 ns      1986935 ns          345 RowInvRate=198.694ns
BM_ActivitiesToVariant/100000/100/20/5    27408373 ns     27407737 ns           25 RowInvRate=274.077ns
BM_ActivitiesToVariant/10000/1000/20/5     2048174 ns      2048144 ns          349 RowInvRate=204.814ns
BM_ActivitiesToVariant/100000/1000/20/5   28266422 ns     28266604 ns           25 RowInvRate=282.666ns
BM_ActivitiesToVariant/10000/20/40/5       3947074 ns      3946987 ns          177 RowInvRate=394.699ns
BM_ActivitiesToVariant/100000/20/40/5     51737459 ns     51737053 ns           13 RowInvRate=517.371ns
BM_ActivitiesToVariant/10000/100/40/5      3967189 ns      3967120 ns          131 RowInvRate=396.712ns
BM_ActivitiesToVariant/100000/100/40/5    53001450 ns     52998674 ns           14 RowInvRate=529.987ns
BM_ActivitiesToVariant/10000/1000/40/5     4153556 ns      4153489 ns          128 RowInvRate=415.349ns
BM_ActivitiesToVariant/100000/1000/40/5   55013767 ns     55011371 ns           13 RowInvRate=550.114ns
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
