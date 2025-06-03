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
2025-06-01T16:46:02+00:00
Running ./be/build_Release/src/bench/celonis/output/null_to_empty_bench
Run on (32 X 3236.83 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 12.76, 7.19, 5.41
// Args: Number of rows / Array length / Null elements percentage
-------------------------------------------------------------------------------------------------------
Benchmark                                             Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------
BM_NullToEmpty/1000/10/0/iterations:2000           1807 ns         1716 ns         2000 RowInvRate=1.71637ns
BM_NullToEmpty/10000/10/0/iterations:2000          3048 ns         2853 ns         2000 RowInvRate=285.291ps
BM_NullToEmpty/100000/10/0/iterations:2000         6499 ns         6061 ns         2000 RowInvRate=60.6101ps
BM_NullToEmpty/1000/100/0/iterations:2000          2952 ns         2759 ns         2000 RowInvRate=2.75928ns
BM_NullToEmpty/10000/100/0/iterations:2000        44319 ns        43902 ns         2000 RowInvRate=4.39021ns
BM_NullToEmpty/100000/100/0/iterations:2000      510906 ns       509300 ns         2000 RowInvRate=5.093ns
BM_NullToEmpty/1000/10/10/iterations:2000          6678 ns         6639 ns         2000 RowInvRate=6.63852ns
BM_NullToEmpty/10000/10/10/iterations:2000        50277 ns        50283 ns         2000 RowInvRate=5.02827ns
BM_NullToEmpty/100000/10/10/iterations:2000      755666 ns       755605 ns         2000 RowInvRate=7.55605ns
BM_NullToEmpty/1000/100/10/iterations:2000        48432 ns        48446 ns         2000 RowInvRate=48.4463ns
BM_NullToEmpty/10000/100/10/iterations:2000      727975 ns       727758 ns         2000 RowInvRate=72.7758ns
BM_NullToEmpty/100000/100/10/iterations:2000   17861678 ns     17838895 ns         2000 RowInvRate=178.389ns
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
