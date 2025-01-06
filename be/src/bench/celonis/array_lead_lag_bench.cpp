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
2025-01-05T17:01:46+00:00
Running ./be/build_Release/src/bench/celonis/output/array_lead_lag_bench
Run on (32 X 2584.65 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.79, 3.47, 2.26
// Args: Number of rows / Array Length
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
BM_ArrayLeadVarchar/1000/10       472369 ns       472367 ns         1482 RowInvRate=472.367ns
BM_ArrayLeadVarchar/10000/10     4661323 ns      4661278 ns          150 RowInvRate=466.128ns
BM_ArrayLeadVarchar/100000/10   46624115 ns     46623222 ns           15 RowInvRate=466.232ns
BM_ArrayLeadVarchar/1000/20       782526 ns       782454 ns          894 RowInvRate=782.454ns
BM_ArrayLeadVarchar/10000/20     7696146 ns      7695941 ns           91 RowInvRate=769.594ns
BM_ArrayLeadVarchar/100000/20   78353325 ns     78352240 ns            9 RowInvRate=783.522ns
BM_ArrayLeadVarchar/1000/40      1356571 ns      1356498 ns          516 RowInvRate=1.3565us
BM_ArrayLeadVarchar/10000/40    13410336 ns     13409705 ns           52 RowInvRate=1.34097us
BM_ArrayLeadVarchar/100000/40  138451482 ns    138445772 ns            5 RowInvRate=1.38446us

BM_ArrayLagVarchar/1000/10        334449 ns       334435 ns         2098 RowInvRate=334.435ns
BM_ArrayLagVarchar/10000/10      3283562 ns      3283465 ns          212 RowInvRate=328.347ns
BM_ArrayLagVarchar/100000/10    32898075 ns     32896911 ns           21 RowInvRate=328.969ns
BM_ArrayLagVarchar/1000/20        592456 ns       592451 ns         1180 RowInvRate=592.451ns
BM_ArrayLagVarchar/10000/20      5858285 ns      5858110 ns          119 RowInvRate=585.811ns
BM_ArrayLagVarchar/100000/20    60375071 ns     60373288 ns           12 RowInvRate=603.733ns
BM_ArrayLagVarchar/1000/40       1091721 ns      1091710 ns          639 RowInvRate=1091.71ns
BM_ArrayLagVarchar/10000/40     10943643 ns     10943386 ns           64 RowInvRate=1094.34ns
BM_ArrayLagVarchar/100000/40   115321344 ns    115319354 ns            6 RowInvRate=1.15319us
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
