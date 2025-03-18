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
2025-03-15T15:44:12+00:00
Running ./be/build_Release/src/bench/celonis/output/array_avg_bench
Run on (32 X 2878.52 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.89, 3.11, 2.76
-------------------------------------------------------------------------------
Benchmark                     Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------
BM_ArrayAvg/1000/20       18949 ns        18863 ns        36933 RowInvRate=18.8626ns
BM_ArrayAvg/10000/20     174655 ns       174666 ns         4003 RowInvRate=17.4666ns
BM_ArrayAvg/1000/40       33150 ns        33061 ns        20861 RowInvRate=33.0611ns
BM_ArrayAvg/10000/40     304176 ns       304052 ns         2324 RowInvRate=30.4052ns
BM_ArrayAvg/1000/80       60535 ns        60486 ns        11711 RowInvRate=60.4859ns
BM_ArrayAvg/10000/80     607857 ns       607498 ns         1167 RowInvRate=60.7498ns
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
