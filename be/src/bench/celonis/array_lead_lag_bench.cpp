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
2025-03-04T02:35:44+00:00
Running ./be/build_Release/src/bench/celonis/output/array_lead_lag_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.91, 7.88, 5.92
// Args: Number of rows / Array Length
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
BM_ArrayLeadVarchar/1000/10       360153 ns       360130 ns         1944 RowInvRate=360.13ns
BM_ArrayLeadVarchar/10000/10     3541139 ns      3540997 ns          198 RowInvRate=354.1ns
BM_ArrayLeadVarchar/100000/10   35169526 ns     35166655 ns           20 RowInvRate=351.667ns
BM_ArrayLeadVarchar/1000/20       633375 ns       633346 ns         1104 RowInvRate=633.346ns
BM_ArrayLeadVarchar/10000/20     6257832 ns      6257041 ns          112 RowInvRate=625.704ns
BM_ArrayLeadVarchar/100000/20   64358176 ns     64356570 ns           11 RowInvRate=643.566ns
BM_ArrayLeadVarchar/1000/40      1162240 ns      1162182 ns          604 RowInvRate=1.16218us
BM_ArrayLeadVarchar/10000/40    11498003 ns     11497921 ns           61 RowInvRate=1.14979us
BM_ArrayLeadVarchar/100000/40  119716315 ns    119700898 ns            6 RowInvRate=1.19701us

BM_ArrayLagVarchar/1000/10        320303 ns       320281 ns         2200 RowInvRate=320.281ns
BM_ArrayLagVarchar/10000/10      3207321 ns      3207288 ns          220 RowInvRate=320.729ns
BM_ArrayLagVarchar/100000/10    30911699 ns     30911017 ns           23 RowInvRate=309.11ns
BM_ArrayLagVarchar/1000/20        571049 ns       571026 ns         1226 RowInvRate=571.026ns
BM_ArrayLagVarchar/10000/20      5757666 ns      5757623 ns          125 RowInvRate=575.762ns
BM_ArrayLagVarchar/100000/20    58492477 ns     58489105 ns           12 RowInvRate=584.891ns
BM_ArrayLagVarchar/1000/40       1072109 ns      1071942 ns          658 RowInvRate=1071.94ns
BM_ArrayLagVarchar/10000/40     10526636 ns     10525819 ns           66 RowInvRate=1052.58ns
BM_ArrayLagVarchar/100000/40   109026878 ns    109015143 ns            6 RowInvRate=1090.15ns
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
