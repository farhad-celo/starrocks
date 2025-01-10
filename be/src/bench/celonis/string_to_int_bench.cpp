#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-01-06T18:00:18+00:00
Running ./be/build_Release/src/bench/celonis/output/string_to_int_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 13.15, 50.59, 39.88
// Number or rows
--------------------------------------------------------------------------------
Benchmark                      Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------
BM_StringToInt/1000       106130 ns       106120 ns         6598 RowInvRate=106.12ns
BM_StringToInt/10000     1051672 ns      1051630 ns          665 RowInvRate=105.163ns
BM_StringToInt/100000   10488983 ns     10488687 ns           67 RowInvRate=104.887ns
*/

static void BM_StringToInt(benchmark::State& state) {
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<long> dist(std::numeric_limits<long>::min(),
                                             std::numeric_limits<long>::max());

    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(Slice(std::to_string(dist(rng))));
        }
        ctx->set_constant_columns({nullptr});

        state.ResumeTiming();
        EXPECT_TRUE(CelonisStringFunctions::string_to_int(ctx.get(), {input_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number or rows
BENCHMARK(BM_StringToInt)->ArgsProduct({{1000, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();
