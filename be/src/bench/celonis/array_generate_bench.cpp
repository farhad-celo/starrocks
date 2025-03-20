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
2025-03-19T22:48:12+00:00
Running ./be/build_Release/src/bench/celonis/output/array_generate_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 11.59, 12.27, 10.16
// Number of rows / Output row length
---------------------------------------------------------------------------------------
Benchmark                             Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------
BM_ArrayGenerate/1000/100       2175607 ns      2175738 ns          324 RowInvRate=2.17574us
BM_ArrayGenerate/10000/100     21767753 ns     21767199 ns           32 RowInvRate=2.17672us
BM_ArrayGenerate/100000/100   231008231 ns    231003656 ns            3 RowInvRate=2.31004us
BM_ArrayGenerate/1000/1000     21236103 ns     21235991 ns           33 RowInvRate=21.236us
BM_ArrayGenerate/10000/1000   229587532 ns    229575371 ns            3 RowInvRate=22.9575us
BM_ArrayGenerate/100000/1000 2245083260 ns   2245005027 ns            1 RowInvRate=22.4501us
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
