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
2025-04-13T19:44:39+00:00
Running ./be/build_Release/src/bench/celonis/output/array_lead_lag_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.97, 3.46, 2.19
// Args: Number of rows / Array Length
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
BM_ArrayLeadVarchar/1000/10       361693 ns       361676 ns         1934 RowInvRate=361.676ns
BM_ArrayLeadVarchar/10000/10     3533008 ns      3532999 ns          198 RowInvRate=353.3ns
BM_ArrayLeadVarchar/100000/10   35297861 ns     35296787 ns           20 RowInvRate=352.968ns
BM_ArrayLeadVarchar/1000/20       631006 ns       630957 ns         1105 RowInvRate=630.957ns
BM_ArrayLeadVarchar/10000/20     6237935 ns      6237775 ns          113 RowInvRate=623.777ns
BM_ArrayLeadVarchar/100000/20   64158920 ns     64158333 ns           11 RowInvRate=641.583ns
BM_ArrayLeadVarchar/1000/40      1181208 ns      1181146 ns          600 RowInvRate=1.18115us
BM_ArrayLeadVarchar/10000/40    11495336 ns     11495352 ns           61 RowInvRate=1.14954us
BM_ArrayLeadVarchar/100000/40  118743678 ns    118739433 ns            6 RowInvRate=1.18739us

BM_ArrayLagVarchar/1000/10        179901 ns       179877 ns         3895 RowInvRate=179.877ns
BM_ArrayLagVarchar/10000/10      1738559 ns      1738529 ns          404 RowInvRate=173.853ns
BM_ArrayLagVarchar/100000/10    22690605 ns     22296394 ns           41 RowInvRate=222.964ns
BM_ArrayLagVarchar/1000/20        471238 ns       471173 ns         1201 RowInvRate=471.173ns
BM_ArrayLagVarchar/10000/20      3289908 ns      3289759 ns          206 RowInvRate=328.976ns
BM_ArrayLagVarchar/100000/20    33891312 ns     33890780 ns           21 RowInvRate=338.908ns
BM_ArrayLagVarchar/1000/40        583680 ns       583628 ns         1192 RowInvRate=583.628ns
BM_ArrayLagVarchar/10000/40      5815463 ns      5815111 ns          124 RowInvRate=581.511ns
BM_ArrayLagVarchar/100000/40    59481300 ns     59481282 ns           11 RowInvRate=594.813ns
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
        auto offset_column =
                ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BIGINT), true);
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
