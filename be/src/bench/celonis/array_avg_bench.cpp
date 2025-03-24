#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_avg.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-15T15:59:37+00:00
Running ./be/build_Release/src/bench/celonis/output/array_avg_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.08, 6.12, 4.58
// Args: Number of rows / Array length
-------------------------------------------------------------------------------
Benchmark                     Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------
BM_ArrayAvg/1000/20       18774 ns        18686 ns        35533 RowInvRate=18.6856ns
BM_ArrayAvg/10000/20     173873 ns       173790 ns         4094 RowInvRate=17.379ns
BM_ArrayAvg/1000/40       31383 ns        31270 ns        22281 RowInvRate=31.2696ns
BM_ArrayAvg/10000/40     295767 ns       295707 ns         2399 RowInvRate=29.5707ns
BM_ArrayAvg/1000/80       58315 ns        58293 ns        12078 RowInvRate=58.2932ns
BM_ArrayAvg/10000/80     585060 ns       584841 ns         1205 RowInvRate=58.4841ns
*/

static void BM_ArrayAvg(benchmark::State& state) {
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
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            for (int j = 0; j < array_length; j++) {
                input_array.push_back(dis(gen));
            }
            input_column->append_datum(input_array);
        }
        state.ResumeTiming();
        auto result = CelonisArrayAvg<TYPE_DOUBLE>::array_avg(ctx.get(), {input_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Array length
BENCHMARK(BM_ArrayAvg)->ArgsProduct({{1000, 10000}, {20, 40, 80}});

} // namespace starrocks

BENCHMARK_MAIN();
