#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/qnorm.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-05-11T11:33:11+00:00
Running ./be/build_Release/src/bench/celonis/output/qnorm_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.52, 9.12, 8.94
// Number of rows
--------------------------------------------------------------------------
Benchmark                Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------
BM_Qnorm/1000        94719 ns        94722 ns         7411 RowInvRate=94.7217ns
BM_Qnorm/10000      946970 ns       947004 ns          746 RowInvRate=94.7004ns
BM_Qnorm/100000    9440371 ns      9439726 ns           73 RowInvRate=94.3973ns
*/

static void BM_Qnorm(benchmark::State& state) {

    std::random_device rd;
    std::mt19937 gen(rd());

    // Create a uniform real distribution between 0.0 and 1.0
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(dis(gen));
        }
        ctx->set_constant_columns({nullptr});

        state.ResumeTiming();
        EXPECT_TRUE(CelonisQnorm::qnorm(ctx.get(), {input_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows
BENCHMARK(BM_Qnorm)->ArgsProduct({{1000, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();
