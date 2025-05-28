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
2025-05-28T01:38:40+00:00
Running ./be/build_Release/src/bench/celonis/output/null_to_empty_bench
Run on (32 X 3018.74 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.79, 3.89, 2.41
// Args: Number of rows / Array length / Null elements percentage
-------------------------------------------------------------------------------------------------------
Benchmark                                             Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------
BM_NullToEmpty/1000/10/0/iterations:2000           1869 ns         1751 ns         2000 RowInvRate=1.7508ns
BM_NullToEmpty/10000/10/0/iterations:2000          2921 ns         2731 ns         2000 RowInvRate=273.124ps
BM_NullToEmpty/100000/10/0/iterations:2000         5422 ns         5010 ns         2000 RowInvRate=50.1029ps
BM_NullToEmpty/1000/100/0/iterations:2000          2443 ns         2286 ns         2000 RowInvRate=2.28569ns
BM_NullToEmpty/10000/100/0/iterations:2000        40424 ns        40300 ns         2000 RowInvRate=4.03001ns
BM_NullToEmpty/100000/100/0/iterations:2000      485913 ns       484465 ns         2000 RowInvRate=4.84465ns
BM_NullToEmpty/1000/10/10/iterations:2000         49248 ns        49252 ns         2000 RowInvRate=49.2518ns
BM_NullToEmpty/10000/10/10/iterations:2000       456958 ns       456969 ns         2000 RowInvRate=45.6969ns
BM_NullToEmpty/100000/10/10/iterations:2000     5014906 ns      5014640 ns         2000 RowInvRate=50.1464ns
BM_NullToEmpty/1000/100/10/iterations:2000       179715 ns       179734 ns         2000 RowInvRate=179.734ns
BM_NullToEmpty/10000/100/10/iterations:2000     2579224 ns      2579152 ns         2000 RowInvRate=257.915ns
BM_NullToEmpty/100000/100/10/iterations:2000   49168663 ns     49164198 ns         2000 RowInvRate=491.642ns
*/

static void BM_NullToEmpty(benchmark::State& state) {
    int num_rows = state.range(0);
    int array_length = state.range(1);
    double null_probability = state.range(2) / 100.0;

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

// Args: Number of rows / Array length / Null elements percentage
BENCHMARK(BM_NullToEmpty)->ArgsProduct({{1000, 10000, 100000}, {10, 100}, {0, 10}})->Iterations(2000);

} // namespace starrocks

BENCHMARK_MAIN();
