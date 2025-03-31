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
2025-03-29T16:58:10+00:00
Running ./be/build_Release/src/bench/celonis/output/shortened_variant_bench
Run on (32 X 2852.16 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.10, 5.98, 4.66
// Number of rows / Number of distinct activities / Variant length / Cycle length
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_ShortenedVariant/10000/20/20/1       4933164 ns      4933113 ns          142 RowInvRate=493.311ns
BM_ShortenedVariant/100000/20/20/1     57106948 ns     57107116 ns           12 RowInvRate=571.071ns
BM_ShortenedVariant/10000/100/20/1      4436434 ns      4436278 ns          159 RowInvRate=443.628ns
BM_ShortenedVariant/100000/100/20/1    51855958 ns     51854871 ns           14 RowInvRate=518.549ns
BM_ShortenedVariant/10000/1000/20/1     4593336 ns      4592626 ns          152 RowInvRate=459.263ns
BM_ShortenedVariant/100000/1000/20/1   53727911 ns     53726105 ns           13 RowInvRate=537.261ns
BM_ShortenedVariant/10000/20/40/1       9777662 ns      9777158 ns           69 RowInvRate=977.716ns
BM_ShortenedVariant/100000/20/40/1    115530968 ns    115531289 ns            6 RowInvRate=1.15531us
BM_ShortenedVariant/10000/100/40/1      8780967 ns      8780610 ns           80 RowInvRate=878.061ns
BM_ShortenedVariant/100000/100/40/1   103254938 ns    103252152 ns            7 RowInvRate=1032.52ns
BM_ShortenedVariant/10000/1000/40/1     9136186 ns      9136343 ns           77 RowInvRate=913.634ns
BM_ShortenedVariant/100000/1000/40/1  107271361 ns    107267783 ns            7 RowInvRate=1072.68ns
BM_ShortenedVariant/10000/20/20/2       5130374 ns      5129612 ns          136 RowInvRate=512.961ns
BM_ShortenedVariant/100000/20/20/2     62703652 ns     62701218 ns           12 RowInvRate=627.012ns
BM_ShortenedVariant/10000/100/20/2      4444217 ns      4444083 ns          157 RowInvRate=444.408ns
BM_ShortenedVariant/100000/100/20/2    56831968 ns     56831315 ns           13 RowInvRate=568.313ns
BM_ShortenedVariant/10000/1000/20/2     4594468 ns      4593923 ns          151 RowInvRate=459.392ns
BM_ShortenedVariant/100000/1000/20/2   58644641 ns     58643428 ns           13 RowInvRate=586.434ns
BM_ShortenedVariant/10000/20/40/2      10211956 ns     10211812 ns           69 RowInvRate=1021.18ns
BM_ShortenedVariant/100000/20/40/2    118964596 ns    118959976 ns            6 RowInvRate=1.1896us
BM_ShortenedVariant/10000/100/40/2      8859954 ns      8859914 ns           78 RowInvRate=885.991ns
BM_ShortenedVariant/100000/100/40/2   105675726 ns    105667949 ns            7 RowInvRate=1056.68ns
BM_ShortenedVariant/10000/1000/40/2     9200948 ns      9200786 ns           75 RowInvRate=920.079ns
BM_ShortenedVariant/100000/1000/40/2  108152937 ns    108153565 ns            6 RowInvRate=1081.54ns
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
