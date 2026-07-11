#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/shortened_variant.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2026-07-11T00:15:11+00:00
Running ./be/build_Release/src/bench/celonis/output/shortened_variant_bench
Run on (32 X 2500 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 1024 KiB (x16)
  L3 Unified 36608 KiB (x1)
Load Average: 0.56, 11.05, 33.70
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_ShortenedVariantString/10000/20/20/1       3216372 ns      3216453 ns          223 RowInvRate=321.645ns
BM_ShortenedVariantString/100000/20/20/1     75206074 ns     75204060 ns            9 RowInvRate=752.041ns
BM_ShortenedVariantString/10000/100/20/1      2515780 ns      2514719 ns          277 RowInvRate=251.472ns
BM_ShortenedVariantString/100000/100/20/1    70015231 ns     70012549 ns           10 RowInvRate=700.125ns
BM_ShortenedVariantString/10000/1000/20/1     2459202 ns      2459078 ns          277 RowInvRate=245.908ns
BM_ShortenedVariantString/100000/1000/20/1   71832565 ns     71830608 ns           10 RowInvRate=718.306ns
BM_ShortenedVariantString/10000/20/40/1       6621493 ns      6620544 ns          107 RowInvRate=662.054ns
BM_ShortenedVariantString/100000/20/40/1    155080522 ns    155080303 ns            4 RowInvRate=1.5508us
BM_ShortenedVariantString/10000/100/40/1      5238337 ns      5236740 ns          129 RowInvRate=523.674ns
BM_ShortenedVariantString/100000/100/40/1   144470437 ns    144460893 ns            5 RowInvRate=1.44461us
BM_ShortenedVariantString/10000/1000/40/1     5353226 ns      5353183 ns          126 RowInvRate=535.318ns
BM_ShortenedVariantString/100000/1000/40/1  148171403 ns    148159399 ns            5 RowInvRate=1.48159us
BM_ShortenedVariantString/10000/20/20/2       3246478 ns      3246603 ns          211 RowInvRate=324.66ns
BM_ShortenedVariantString/100000/20/20/2     77117259 ns     77112389 ns            9 RowInvRate=771.124ns
BM_ShortenedVariantString/10000/100/20/2      2531570 ns      2530710 ns          272 RowInvRate=253.071ns
BM_ShortenedVariantString/100000/100/20/2    70888883 ns     70884391 ns           10 RowInvRate=708.844ns
BM_ShortenedVariantString/10000/1000/20/2     2500622 ns      2500759 ns          266 RowInvRate=250.076ns
BM_ShortenedVariantString/100000/1000/20/2   72160880 ns     72160718 ns           10 RowInvRate=721.607ns
BM_ShortenedVariantString/10000/20/40/2       6722634 ns      6718966 ns          101 RowInvRate=671.897ns
BM_ShortenedVariantString/100000/20/40/2    157562404 ns    157548562 ns            4 RowInvRate=1.57549us
BM_ShortenedVariantString/10000/100/40/2      5425927 ns      5424433 ns          117 RowInvRate=542.443ns
BM_ShortenedVariantString/100000/100/40/2   144855904 ns    144853512 ns            5 RowInvRate=1.44854us
BM_ShortenedVariantString/10000/1000/40/2     5256184 ns      5256248 ns          117 RowInvRate=525.625ns
BM_ShortenedVariantString/100000/1000/40/2  147148319 ns    147147871 ns            5 RowInvRate=1.47148us
BM_ShortenedVariantInt/10000/20/20/1           903956 ns       903991 ns          793 RowInvRate=90.3991ns
BM_ShortenedVariantInt/100000/20/20/1        19437747 ns     19435830 ns           36 RowInvRate=194.358ns
BM_ShortenedVariantInt/10000/100/20/1          772485 ns       772493 ns          934 RowInvRate=77.2493ns
BM_ShortenedVariantInt/100000/100/20/1       18383551 ns     18382155 ns           37 RowInvRate=183.822ns
BM_ShortenedVariantInt/10000/1000/20/1         735351 ns       735339 ns          990 RowInvRate=73.5339ns
BM_ShortenedVariantInt/100000/1000/20/1      18352803 ns     18350867 ns           38 RowInvRate=183.509ns
BM_ShortenedVariantInt/10000/20/40/1          1777509 ns      1777611 ns          402 RowInvRate=177.761ns
BM_ShortenedVariantInt/100000/20/40/1        43077258 ns     43076935 ns           16 RowInvRate=430.769ns
BM_ShortenedVariantInt/10000/100/40/1         1572514 ns      1572522 ns          441 RowInvRate=157.252ns
BM_ShortenedVariantInt/100000/100/40/1       42003299 ns     42000084 ns           17 RowInvRate=420.001ns
BM_ShortenedVariantInt/10000/1000/40/1        1517808 ns      1517897 ns          447 RowInvRate=151.79ns
BM_ShortenedVariantInt/100000/1000/40/1      41401730 ns     41399107 ns           17 RowInvRate=413.991ns
BM_ShortenedVariantInt/10000/20/20/2           915624 ns       915560 ns          781 RowInvRate=91.556ns
BM_ShortenedVariantInt/100000/20/20/2        20178660 ns     20176388 ns           35 RowInvRate=201.764ns
BM_ShortenedVariantInt/10000/100/20/2          784353 ns       784320 ns          918 RowInvRate=78.432ns
BM_ShortenedVariantInt/100000/100/20/2       18774867 ns     18773271 ns           37 RowInvRate=187.733ns
BM_ShortenedVariantInt/10000/1000/20/2         730640 ns       730644 ns          983 RowInvRate=73.0644ns
BM_ShortenedVariantInt/100000/1000/20/2      18166379 ns     18165346 ns           39 RowInvRate=181.653ns
BM_ShortenedVariantInt/10000/20/40/2          1779309 ns      1779376 ns          379 RowInvRate=177.938ns
BM_ShortenedVariantInt/100000/20/40/2        44480968 ns     44478734 ns           16 RowInvRate=444.787ns
BM_ShortenedVariantInt/10000/100/40/2         1532527 ns      1532581 ns          447 RowInvRate=153.258ns
BM_ShortenedVariantInt/100000/100/40/2       42100193 ns     42099990 ns           16 RowInvRate=421ns
BM_ShortenedVariantInt/10000/1000/40/2        1512722 ns      1512750 ns          456 RowInvRate=151.275ns
BM_ShortenedVariantInt/100000/1000/40/2      41255887 ns     41254121 ns           17 RowInvRate=412.541ns
*/

static void do_bench_string(benchmark::State& state) {
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

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                                                        TypeDescriptor::from_logical_type(TYPE_BIGINT)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_ARRAY);
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
        ASSERT_TRUE(CelonisShortenedVariant<TYPE_VARCHAR>::celonis_shortened_variant(
                            ctx.get(), {variant_column, cycle_length_column})
                            .ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void do_bench_int(benchmark::State& state) {
    const int num_rows = state.range(0);
    const int num_distinct_activities = state.range(1);
    const int variant_length = state.range(2);
    const int64_t cycle_length = state.range(3);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_distinct_activities - 1);

    std::vector<int32_t> values;
    values.reserve(num_distinct_activities);
    for (int i = 0; i < num_distinct_activities; i++) {
        values.push_back(i + 1);
    }

    auto gen_rand_element = [&]() { return values[uniform_value(rng)]; };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                                                        TypeDescriptor::from_logical_type(TYPE_BIGINT)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_ARRAY);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto variant_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::from_logical_type(TYPE_INT)), false);
        auto cycle_length_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BIGINT), false);
        for (int i = 0; i < num_rows; i++) {
            variant_column->append_datum(gen_rand_array(variant_length));
            cycle_length_column->append_datum(cycle_length);
        }
        ctx->set_constant_columns({nullptr, cycle_length_column});

        state.ResumeTiming();
        ASSERT_TRUE(CelonisShortenedVariant<TYPE_INT>::celonis_shortened_variant(ctx.get(),
                                                                                 {variant_column, cycle_length_column})
                            .ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ShortenedVariantString(benchmark::State& state) {
    do_bench_string(state);
}

static void BM_ShortenedVariantInt(benchmark::State& state) {
    do_bench_int(state);
}

// Number of rows / Number of distinct activities / Variant length / Cycle length
BENCHMARK(BM_ShortenedVariantString)->ArgsProduct({{10000, 100000}, {20, 100, 1000}, {20, 40}, {1, 2}});
BENCHMARK(BM_ShortenedVariantInt)->ArgsProduct({{10000, 100000}, {20, 100, 1000}, {20, 40}, {1, 2}});

} // namespace starrocks

BENCHMARK_MAIN();
