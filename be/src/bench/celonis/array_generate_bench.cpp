#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/array_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-06-07T16:34:25+00:00
Running ./be/build_Release/src/bench/celonis/output/array_generate_bench
Run on (32 X 3174.92 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.94, 1.70, 1.32
// Number of rows / Output row length
---------------------------------------------------------------------------------------
Benchmark                             Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------
BM_ArrayGenerate/1000/100        387599 ns       387724 ns         1834 RowInvRate=387.724ns
BM_ArrayGenerate/10000/100      8179409 ns      8179348 ns          120 RowInvRate=817.935ns
BM_ArrayGenerate/100000/100    46429038 ns     46429188 ns           11 RowInvRate=464.292ns
BM_ArrayGenerate/1000/1000      6188408 ns      6188456 ns          131 RowInvRate=6.18846us
BM_ArrayGenerate/10000/1000    46403722 ns     46401774 ns           15 RowInvRate=4.64018us
BM_ArrayGenerate/100000/1000  458729257 ns    458727012 ns            2 RowInvRate=4.58727us
*/

static void BM_ArrayGenerate(benchmark::State& state) {
    int num_rows = state.range(0);
    int64_t start = 0;
    int64_t stop = state.range(1) - 1;
    int64_t step = 1;

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr start_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        ColumnPtr stop_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        ColumnPtr step_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        for (int i = 0; i < num_rows; i++) {
            start_column->append_datum(start);
            stop_column->append_datum(stop);
            step_column->append_datum(step);
        }
        ctx->set_constant_columns({nullptr, nullptr, nullptr});

        state.ResumeTiming();
        EXPECT_TRUE(
                ArrayFunctions::array_generate<TYPE_BIGINT>(ctx.get(), {start_column, stop_column, step_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows / Output row length
BENCHMARK(BM_ArrayGenerate)->ArgsProduct({{1000, 10000, 100000}, {100, 1000}});

} // namespace starrocks

BENCHMARK_MAIN();
