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
2025-03-15T02:29:40+00:00
Running ./be/build_Release/src/bench/celonis/output/array_first_last_bench
Run on (32 X 2762.96 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.46, 0.85, 1.86
// Args: Number of rows / Null percentage / Array Length
--------------------------------------------------------------------------------------------
Benchmark                                  Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------
BM_ArrayFirstVarchar/10000/0/20       363080 ns       363072 ns         1936 RowInvRate=36.3072ns
BM_ArrayFirstVarchar/10000/50/20      486326 ns       486339 ns         1453 RowInvRate=48.6339ns
BM_ArrayFirstVarchar/10000/100/20     432774 ns       432771 ns         1634 RowInvRate=43.2771ns
BM_ArrayFirstVarchar/10000/0/40       566988 ns       566941 ns         1241 RowInvRate=56.6941ns
BM_ArrayFirstVarchar/10000/50/40      683115 ns       682992 ns         1028 RowInvRate=68.2992ns
BM_ArrayFirstVarchar/10000/100/40     730424 ns       730343 ns          984 RowInvRate=73.0343ns

BM_ArrayLastVarchar/10000/0/20        383381 ns       383449 ns         1821 RowInvRate=38.3449ns
BM_ArrayLastVarchar/10000/50/20       484168 ns       484175 ns         1451 RowInvRate=48.4175ns
BM_ArrayLastVarchar/10000/100/20      428046 ns       428049 ns         1589 RowInvRate=42.8049ns
BM_ArrayLastVarchar/10000/0/40        576658 ns       576638 ns         1180 RowInvRate=57.6638ns
BM_ArrayLastVarchar/10000/50/40       698600 ns       698496 ns         1039 RowInvRate=69.8496ns
BM_ArrayLastVarchar/10000/100/40      732284 ns       732183 ns          981 RowInvRate=73.2183ns
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
