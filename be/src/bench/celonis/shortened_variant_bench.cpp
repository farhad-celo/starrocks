#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/shortened_variant.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-31T19:30:27+00:00
Running ./be/build_Release/src/bench/celonis/output/shortened_variant_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.16, 4.79, 4.25
// Number of rows / Number of distinct activities / Variant length / Cycle length
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_ShortenedVariant/10000/20/20/1       2968020 ns      2967929 ns          235 RowInvRate=296.793ns
BM_ShortenedVariant/100000/20/20/1     36194517 ns     36193444 ns           20 RowInvRate=361.934ns
BM_ShortenedVariant/10000/100/20/1      2370904 ns      2370765 ns          294 RowInvRate=237.076ns
BM_ShortenedVariant/100000/100/20/1    30251590 ns     30250961 ns           23 RowInvRate=302.51ns
BM_ShortenedVariant/10000/1000/20/1     2418646 ns      2418500 ns          291 RowInvRate=241.85ns
BM_ShortenedVariant/100000/1000/20/1   30482567 ns     30482224 ns           23 RowInvRate=304.822ns
BM_ShortenedVariant/10000/20/40/1       6117496 ns      6117431 ns          114 RowInvRate=611.743ns
BM_ShortenedVariant/100000/20/40/1     74542357 ns     74541416 ns           10 RowInvRate=745.414ns
BM_ShortenedVariant/10000/100/40/1      4780091 ns      4780284 ns          139 RowInvRate=478.028ns
BM_ShortenedVariant/100000/100/40/1    60483472 ns     60480965 ns           12 RowInvRate=604.81ns
BM_ShortenedVariant/10000/1000/40/1     4748152 ns      4748155 ns          146 RowInvRate=474.815ns
BM_ShortenedVariant/100000/1000/40/1   63109639 ns     63107035 ns           11 RowInvRate=631.07ns
BM_ShortenedVariant/10000/20/20/2       3040914 ns      3040578 ns          228 RowInvRate=304.058ns
BM_ShortenedVariant/100000/20/20/2     41264541 ns     41263969 ns           17 RowInvRate=412.64ns
BM_ShortenedVariant/10000/100/20/2      2411073 ns      2411057 ns          287 RowInvRate=241.106ns
BM_ShortenedVariant/100000/100/20/2    35567971 ns     35566484 ns           20 RowInvRate=355.665ns
BM_ShortenedVariant/10000/1000/20/2     2396993 ns      2396519 ns          292 RowInvRate=239.652ns
BM_ShortenedVariant/100000/1000/20/2   33214420 ns     33209464 ns           21 RowInvRate=332.095ns
BM_ShortenedVariant/10000/20/40/2       6304151 ns      6304137 ns          107 RowInvRate=630.414ns
BM_ShortenedVariant/100000/20/40/2     76848827 ns     76846727 ns            9 RowInvRate=768.467ns
BM_ShortenedVariant/10000/100/40/2      4870201 ns      4870294 ns          126 RowInvRate=487.029ns
BM_ShortenedVariant/100000/100/40/2    63083733 ns     63082253 ns           11 RowInvRate=630.823ns
BM_ShortenedVariant/10000/1000/40/2     4814588 ns      4814521 ns          145 RowInvRate=481.452ns
BM_ShortenedVariant/100000/1000/40/2   62827303 ns     62824392 ns           11 RowInvRate=628.244ns
*/

static void do_bench(benchmark::State& state) {
    const int num_rows = state.range(0);
    const int num_distinct_activities = state.range(1);
    const int variant_length = state.range(2);
    const int64_t cycle_length = state.range(3);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_distinct_activities - 1);

    std::vector<std::string> values;
    values.reserve(num_distinct_activities);
    for (int i = 0; i < num_distinct_activities; i++) {
        values.push_back("Activity" + std::to_string(i));
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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto variant_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto cycle_length_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BIGINT), false);
        for (int i = 0; i < num_rows; i++) {
            variant_column->append_datum(gen_rand_array(variant_length));
            cycle_length_column->append_datum(cycle_length);
        }
        ctx->set_constant_columns({nullptr, cycle_length_column});

        state.ResumeTiming();
        ASSERT_TRUE(CelonisShortenedVariant::celonis_shortened_variant(ctx.get(),
                                                                       {variant_column, cycle_length_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ShortenedVariant(benchmark::State& state) {
    do_bench(state);
}

// Number of rows / Number of distinct activities / Variant length / Cycle length
BENCHMARK(BM_ShortenedVariant) ->ArgsProduct({{10000, 100000}, {20, 100, 1000}, {20, 40}, {1, 2}});

} // namespace starrocks

BENCHMARK_MAIN();
