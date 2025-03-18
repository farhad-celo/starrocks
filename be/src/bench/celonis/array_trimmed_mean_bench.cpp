#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_trimmed_mean.cpp"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-17T11:48:07+00:00
Running ./be/build_Release/src/bench/celonis/output/array_trimmed_mean_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.23, 2.90, 1.46
// Args: Number of rows / Array length
---------------------------------------------------------------------------------------
Benchmark                             Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------
BM_ArrayTrimmedMean/1000/20      404694 ns       404635 ns         1730 RowInvRate=404.635ns
BM_ArrayTrimmedMean/10000/20    3933692 ns      3933305 ns          178 RowInvRate=393.331ns
BM_ArrayTrimmedMean/1000/40     1023664 ns      1023547 ns          684 RowInvRate=1023.55ns
BM_ArrayTrimmedMean/10000/40   10086255 ns     10085821 ns           69 RowInvRate=1008.58ns
BM_ArrayTrimmedMean/1000/80     2451123 ns      2450888 ns          286 RowInvRate=2.45089us
BM_ArrayTrimmedMean/10000/80   24385160 ns     24383756 ns           29 RowInvRate=2.43838us
*/

static void BM_ArrayTrimmedMean(benchmark::State& state) {
    int num_rows = state.range(0);
    int array_length = state.range(1);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DOUBLE)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dis(-10000.0, 10000.0);

    int total_rows = 0;
    for (auto _: state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DOUBLE)), false);
        auto lower_cutoff_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        lower_cutoff_column->append_datum(5L);
        lower_cutoff_column = ConstColumn::create(lower_cutoff_column, num_rows);
        auto upper_cutoff_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        upper_cutoff_column->append_datum(5L);
        upper_cutoff_column = ConstColumn::create(upper_cutoff_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            for (int j = 0; j < array_length; j++) {
                input_array.push_back(dis(gen));
            }
            input_column->append_datum(input_array);
        }
        state.ResumeTiming();
        auto result = CelonisArrayTrimmedMean<TYPE_DOUBLE>::celonis_array_trimmed_mean(ctx.get(), {input_column,
                                                                                                   lower_cutoff_column,
                                                                                                   upper_cutoff_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Array length
BENCHMARK(BM_ArrayTrimmedMean)->ArgsProduct({{1000, 10000}, {20, 40, 80}});

} // namespace starrocks

BENCHMARK_MAIN();
