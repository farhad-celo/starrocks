#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/encode_variant.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2024-11-23T02:45:53+00:00
Running ./be/build_Release/src/bench/celonis/output/encode_variant_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.25, 1.41, 1.32
// Args: Number of rows / Variant Length / Length of Activity Array
------------------------------------------------------------------------------------------
Benchmark                                Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------------
BM_EncodeVariant/1000/40/40        1270617 ns      1270593 ns          550 RowInvRate=1.27059us
BM_EncodeVariant/10000/40/40      12735570 ns     12735574 ns           55 RowInvRate=1.27356us
BM_EncodeVariant/100000/40/40    134634732 ns    134632898 ns            5 RowInvRate=1.34633us
BM_EncodeVariant/1000/100/40       3140049 ns      3139970 ns          223 RowInvRate=3.13997us
BM_EncodeVariant/10000/100/40     32908212 ns     32907580 ns           21 RowInvRate=3.29076us
BM_EncodeVariant/100000/100/40   338497701 ns    338478045 ns            2 RowInvRate=3.38478us
BM_EncodeVariant/1000/40/200       1238816 ns      1238730 ns          563 RowInvRate=1.23873us
BM_EncodeVariant/10000/40/200     12329917 ns     12328506 ns           56 RowInvRate=1.23285us
BM_EncodeVariant/100000/40/200   131398282 ns    131388887 ns            5 RowInvRate=1.31389us
BM_EncodeVariant/1000/100/200      3043848 ns      3043840 ns          230 RowInvRate=3.04384us
BM_EncodeVariant/10000/100/200    31645312 ns     31644061 ns           22 RowInvRate=3.16441us
BM_EncodeVariant/100000/100/200  331774231 ns    331767153 ns            2 RowInvRate=3.31767us
*/

static void do_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int variant_length = state.range(1);
    int num_activities = state.range(2);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_activities - 1);

    std::vector<std::string> activities;
    activities.reserve(num_activities);
    DatumArray activity_array;
    for (int i = 0; i < num_activities; i++) {
        std::string activity = "activity_" + std::to_string(i);
        activities.push_back(activity);
        activity_array.emplace_back(Slice(activity));
    }

    auto gen_rand_element = [&]() { return Slice(activities[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        array.reserve(num_elements);
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
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
        auto activity_array_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        activity_array_column->append_datum(activity_array);

        ctx->set_constant_columns({nullptr, activity_array_column});
        state.ResumeTiming();
        ASSERT_TRUE(CelonisEncodeVariant::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisEncodeVariant::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisEncodeVariant::encode_variant(ctx.get(), {variant_column, activity_array_column}).ok());
        ASSERT_TRUE(CelonisEncodeVariant::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisEncodeVariant::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_EncodeVariant(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / Variant Length / Length of Activity Array
BENCHMARK(BM_EncodeVariant)->ArgsProduct({{1000, 10000, 100000}, {40, 100}, {40, 200}});

} // namespace starrocks

BENCHMARK_MAIN();