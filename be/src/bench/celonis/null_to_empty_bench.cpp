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
2025-01-04T02:38:49+00:00
Running ./be/build_Release/src/bench/celonis/output/null_to_empty_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.03, 2.98, 2.54
// Args: Number of rows / Array length / Null percentage
---------------------------------------------------------------------------------------
Benchmark                             Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------
BM_NullToEmpty/1000/10/0           8509 ns         8451 ns        87879 RowInvRate=8.4512ns
BM_NullToEmpty/10000/10/0         57290 ns        57301 ns        10773 RowInvRate=5.73006ns
BM_NullToEmpty/100000/10/0       746438 ns       746379 ns          884 RowInvRate=7.46379ns
BM_NullToEmpty/1000/100/0         57436 ns        57458 ns        13279 RowInvRate=57.4582ns
BM_NullToEmpty/10000/100/0       800042 ns       799747 ns          937 RowInvRate=79.9747ns
BM_NullToEmpty/100000/100/0    21862323 ns     21860091 ns           28 RowInvRate=218.601ns
BM_NullToEmpty/1000/10/10          5827 ns         5820 ns       119250 RowInvRate=5.82025ns
BM_NullToEmpty/10000/10/10        45090 ns        45090 ns        15464 RowInvRate=4.50903ns
BM_NullToEmpty/100000/10/10      432955 ns       432984 ns         1618 RowInvRate=4.32984ns
BM_NullToEmpty/1000/100/10         5805 ns         5799 ns       118075 RowInvRate=5.79939ns
BM_NullToEmpty/10000/100/10       45163 ns        45161 ns        15469 RowInvRate=4.51613ns
BM_NullToEmpty/100000/100/10     433237 ns       433249 ns         1613 RowInvRate=4.33249ns
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
BENCHMARK(BM_NullToEmpty)->ArgsProduct({{1000, 10000, 100000}, {10, 100}, {0, 10}});

} // namespace starrocks

BENCHMARK_MAIN();
