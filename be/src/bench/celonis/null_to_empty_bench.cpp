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
2025-01-06T21:46:21+00:00
Running ./be/build_Release/src/bench/celonis/output/null_to_empty_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.47, 8.21, 5.03
// Args: Number of rows / Array length / Null percentage
-------------------------------------------------------------------------------------------------------
Benchmark                                             Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------
BM_NullToEmpty/1000/10/0/iterations:2000           1905 ns         1808 ns         2000 RowInvRate=1.80776ns
BM_NullToEmpty/10000/10/0/iterations:2000          2985 ns         2787 ns         2000 RowInvRate=278.706ps
BM_NullToEmpty/100000/10/0/iterations:2000         6093 ns         5636 ns         2000 RowInvRate=56.3634ps
BM_NullToEmpty/1000/100/0/iterations:2000          2785 ns         2609 ns         2000 RowInvRate=2.60858ns
BM_NullToEmpty/10000/100/0/iterations:2000        43420 ns        42894 ns         2000 RowInvRate=4.28937ns
BM_NullToEmpty/100000/100/0/iterations:2000      491110 ns       489838 ns         2000 RowInvRate=4.89838ns
BM_NullToEmpty/1000/10/10/iterations:2000          6122 ns         6045 ns         2000 RowInvRate=6.04522ns
BM_NullToEmpty/10000/10/10/iterations:2000        46332 ns        46340 ns         2000 RowInvRate=4.634ns
BM_NullToEmpty/100000/10/10/iterations:2000      432625 ns       432665 ns         2000 RowInvRate=4.32665ns
BM_NullToEmpty/1000/100/10/iterations:2000         5856 ns         5858 ns         2000 RowInvRate=5.85778ns
BM_NullToEmpty/10000/100/10/iterations:2000       45701 ns        45702 ns         2000 RowInvRate=4.57015ns
BM_NullToEmpty/100000/100/10/iterations:2000     433832 ns       433863 ns         2000 RowInvRate=4.33863ns
*/

static void BM_NullToEmpty(benchmark::State& state) {
    int num_rows = state.range(0);
    int array_length = state.range(1);
    bool null_probability = state.range(2) / 100.0;

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)))};
    auto return_type =
        AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(null_probability);

    std::vector<std::string> strings;
    strings.reserve(array_length);
    for (int j = 0; j < array_length; ++j) {
        strings.emplace_back("value" + std::to_string(j));
    }
    int total_rows = 0;
    for (auto _: state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        for (int i = 0; i < num_rows; i++) {
            bool is_null = dist(gen);
            if (is_null) {
                input_column->append_datum(kNullDatum);
                continue;
            }
            DatumArray input_array;
            for (int j = 0; j < array_length; j++) {
                input_array.emplace_back(Slice(strings[j]));
            }
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = CelonisArrayFunctions::null_to_empty(ctx.get(), {input_column});

        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Array length / Null percentage
BENCHMARK(BM_NullToEmpty)->ArgsProduct({{1000, 10000, 100000}, {10, 100}, {0, 10}})->Iterations(2000);

} // namespace starrocks

BENCHMARK_MAIN();
