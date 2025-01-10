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
2025-01-06T18:01:11+00:00
Running ./be/build_Release/src/bench/celonis/output/string_to_double_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.74, 42.80, 37.79
// Number of rows
-----------------------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------
BM_StringToDouble/1000       401807 ns       401798 ns         1742 RowInvRate=401.798ns
BM_StringToDouble/10000     3980656 ns      3980435 ns          176 RowInvRate=398.044ns
BM_StringToDouble/100000   39788945 ns     39788097 ns           18 RowInvRate=397.881ns
*/

static void BM_StringToDouble(benchmark::State& state) {
    std::random_device dev;
    std::mt19937 rng(dev());
    double min = -1000000000.0;
    double max = 1000000000.0;

    // Create a uniform real distribution for doubles within the range
    std::uniform_real_distribution<> distrib(min, max);

    int num_rows = state.range(0);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(Slice(std::to_string(distrib(rng))));
        }
        ctx->set_constant_columns({nullptr});

        state.ResumeTiming();
        EXPECT_TRUE(CelonisStringFunctions::string_to_double(ctx.get(), {input_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows
BENCHMARK(BM_StringToDouble)->ArgsProduct({{1000, 10000, 100000}});

} // namespace starrocks

BENCHMARK_MAIN();
