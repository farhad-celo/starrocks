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
2024-11-23T12:20:48+00:00
Running ./be/build_Release/src/bench/celonis/output/encode_variant_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.43, 3.52, 3.35
// Args: Number of rows / Variant Length / Length of Activity Array
------------------------------------------------------------------------------------------
Benchmark                                Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------------
BM_EncodeVariant/1000/40/40         310426 ns       310410 ns         2254 RowInvRate=310.41ns
BM_EncodeVariant/10000/40/40       3060730 ns      3060380 ns          227 RowInvRate=306.038ns
BM_EncodeVariant/100000/40/40     38697460 ns     38696395 ns           18 RowInvRate=386.964ns
BM_EncodeVariant/1000/100/40        740395 ns       740376 ns          946 RowInvRate=740.376ns
BM_EncodeVariant/10000/100/40      8772977 ns      8772944 ns           80 RowInvRate=877.294ns
BM_EncodeVariant/100000/100/40   103935592 ns    103932745 ns            7 RowInvRate=1039.33ns
BM_EncodeVariant/1000/40/200        353023 ns       353006 ns         1981 RowInvRate=353.006ns
BM_EncodeVariant/10000/40/200      3426689 ns      3426489 ns          205 RowInvRate=342.649ns
BM_EncodeVariant/100000/40/200    43296710 ns     43290908 ns           16 RowInvRate=432.909ns
BM_EncodeVariant/1000/100/200       833927 ns       833799 ns          844 RowInvRate=833.799ns
BM_EncodeVariant/10000/100/200     9656947 ns      9655715 ns           73 RowInvRate=965.572ns
BM_EncodeVariant/100000/100/200  111497055 ns    111489981 ns            6 RowInvRate=1.1149us
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
