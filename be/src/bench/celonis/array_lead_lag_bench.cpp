#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_functions.cpp"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {
/*
2025-04-14T16:30:49+00:00
Running ./be/build_Release/src/bench/celonis/output/array_lead_lag_bench
Run on (32 X 3238.9 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.57, 2.23, 3.15
// Args: Number of rows / Array Length
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
BM_ArrayLeadVarchar/1000/10       181304 ns       181311 ns         3877 RowInvRate=181.311ns
BM_ArrayLeadVarchar/10000/10     1758467 ns      1758427 ns          399 RowInvRate=175.843ns
BM_ArrayLeadVarchar/100000/10   17503135 ns     17502985 ns           39 RowInvRate=175.03ns
BM_ArrayLeadVarchar/1000/20       316197 ns       316212 ns         2218 RowInvRate=316.212ns
BM_ArrayLeadVarchar/10000/20     3101212 ns      3100850 ns          228 RowInvRate=310.085ns
BM_ArrayLeadVarchar/100000/20   32141030 ns     32139290 ns           21 RowInvRate=321.393ns
BM_ArrayLeadVarchar/1000/40       564792 ns       564804 ns         1244 RowInvRate=564.804ns
BM_ArrayLeadVarchar/10000/40     5584970 ns      5584427 ns          125 RowInvRate=558.443ns
BM_ArrayLeadVarchar/100000/40   59121523 ns     59115908 ns           12 RowInvRate=591.159ns

BM_ArrayLagVarchar/1000/10        183703 ns       183692 ns         3823 RowInvRate=183.692ns
BM_ArrayLagVarchar/10000/10      1756422 ns      1756254 ns          396 RowInvRate=175.625ns
BM_ArrayLagVarchar/100000/10    17678469 ns     17677896 ns           39 RowInvRate=176.779ns
BM_ArrayLagVarchar/1000/20        315279 ns       315270 ns         2216 RowInvRate=315.27ns
BM_ArrayLagVarchar/10000/20      3080721 ns      3080696 ns          229 RowInvRate=308.07ns
BM_ArrayLagVarchar/100000/20    33751190 ns     33751140 ns           21 RowInvRate=337.511ns
BM_ArrayLagVarchar/1000/40        566004 ns       565999 ns         1240 RowInvRate=565.999ns
BM_ArrayLagVarchar/10000/40      5552678 ns      5552509 ns          126 RowInvRate=555.251ns
BM_ArrayLagVarchar/100000/40    59884434 ns     59881198 ns           12 RowInvRate=598.812ns
*/

using ScalarFunction = StatusOr<ColumnPtr> (*)(FunctionContext* context, const Columns& columns);

static void bench(benchmark::State& state, ScalarFunction scalar_function) {
    int num_rows = state.range(0);
    int array_length = state.range(1);

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(1, array_length - 1);

    std::vector<std::string> values;
    values.reserve(array_length);
    for (int i = 0; i < array_length; i++) {
        values.push_back("value" + std::to_string(i));
    }

    DatumArray input_array;
    for (int j = 0; j < array_length; j++) {
        input_array.emplace_back(Slice(values[j]));
    }

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto offset_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BIGINT), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(input_array);
            offset_column->append_datum(static_cast<int64_t>(uniform_value(rng)));
        }

        state.ResumeTiming();
        auto result = scalar_function(ctx.get(), {input_column, offset_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ArrayLeadVarchar(benchmark::State& state) {
    bench(state, CelonisArrayFunctions::array_lead);
}

static void BM_ArrayLagVarchar(benchmark::State& state) {
    bench(state, CelonisArrayFunctions::array_lag);
}

// Args: Number of rows / Array Length
BENCHMARK(BM_ArrayLeadVarchar)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_ArrayLagVarchar)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
