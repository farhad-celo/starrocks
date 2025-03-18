#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_end_finder.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-15T12:21:30+00:00
Running ./be/build_Release/src/bench/celonis/output/array_first_last_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.08, 6.76, 4.41
// Args: Number of rows / Null percentage / Array Length
--------------------------------------------------------------------------------------------
Benchmark                                  Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------
BM_ArrayFirstVarchar/10000/0/20       358305 ns       358321 ns         1957 RowInvRate=35.8321ns
BM_ArrayFirstVarchar/10000/50/20      475582 ns       475546 ns         1487 RowInvRate=47.5546ns
BM_ArrayFirstVarchar/10000/100/20     358763 ns       358749 ns         1956 RowInvRate=35.8749ns
BM_ArrayFirstVarchar/10000/0/40       569122 ns       569077 ns         1262 RowInvRate=56.9077ns
BM_ArrayFirstVarchar/10000/50/40      679079 ns       678988 ns         1043 RowInvRate=67.8988ns
BM_ArrayFirstVarchar/10000/100/40     652185 ns       652133 ns         1174 RowInvRate=65.2133ns

BM_ArrayLastVarchar/10000/0/20        360974 ns       361073 ns         1944 RowInvRate=36.1073ns
BM_ArrayLastVarchar/10000/50/20       474242 ns       474275 ns         1500 RowInvRate=47.4275ns
BM_ArrayLastVarchar/10000/100/20      355977 ns       355999 ns         1980 RowInvRate=35.5999ns
BM_ArrayLastVarchar/10000/0/40        609343 ns       609398 ns         1268 RowInvRate=60.9398ns
BM_ArrayLastVarchar/10000/50/40       678415 ns       678334 ns          952 RowInvRate=67.8334ns
BM_ArrayLastVarchar/10000/100/40      663562 ns       663527 ns         1164 RowInvRate=66.3527ns
*/

using ScalarFunction = StatusOr<ColumnPtr> (*)(FunctionContext* context, const Columns& columns);

static void bench(benchmark::State& state, ScalarFunction scalar_function) {
    int num_rows = state.range(0);
    double null_probability = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(null_probability);

    std::vector<std::string> strings;
    strings.reserve(array_length);
    for (int j = 0; j < array_length; ++j) {
        strings.emplace_back("value" + std::to_string(j));
    }
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            input_array.reserve(array_length);
            for (int j = 0; j < array_length; j++) {
                bool is_null = dist(gen);
                if (is_null) {
                    input_array.emplace_back(kNullDatum);
                } else {
                    input_array.emplace_back(Slice(strings[j]));
                }
            }
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = scalar_function(ctx.get(), {input_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ArrayFirstVarchar(benchmark::State& state) {
    bench(state, CelonisArrayEndFinder<TYPE_VARCHAR>::array_first);
}

static void BM_ArrayLastVarchar(benchmark::State& state) {
    bench(state, CelonisArrayEndFinder<TYPE_VARCHAR>::array_last);
}

// Args: Number of rows / Null percentage / Array Length
BENCHMARK(BM_ArrayFirstVarchar)->ArgsProduct({{10000}, {0, 50, 100}, {20, 40}});

BENCHMARK(BM_ArrayLastVarchar)->ArgsProduct({{10000}, {0, 50, 100}, {20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
