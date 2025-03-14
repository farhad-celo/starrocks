#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_count_distinct.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-14T15:13:18+00:00
Running ./be/build_Release/src/bench/celonis/output/array_count_distinct_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.81, 2.50, 2.92
// Args: Number of rows / Null percentage / Array length
---------------------------------------------------------------------------------------------------
Benchmark                                         Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------
BM_ArrayCountDistinctVarchar/1000/0/20       796273 ns       796209 ns          886 RowInvRate=796.209ns
BM_ArrayCountDistinctVarchar/10000/0/20     7924203 ns      7923993 ns           88 RowInvRate=792.399ns
BM_ArrayCountDistinctVarchar/1000/10/20      775188 ns       775125 ns          909 RowInvRate=775.125ns
BM_ArrayCountDistinctVarchar/10000/10/20    7623748 ns      7623137 ns           92 RowInvRate=762.314ns
BM_ArrayCountDistinctVarchar/1000/50/20      496496 ns       496454 ns         1418 RowInvRate=496.454ns
BM_ArrayCountDistinctVarchar/10000/50/20    4913594 ns      4913431 ns          142 RowInvRate=491.343ns
BM_ArrayCountDistinctVarchar/1000/0/40      1399362 ns      1399257 ns          501 RowInvRate=1.39926us
BM_ArrayCountDistinctVarchar/10000/0/40    13946382 ns     13945720 ns           50 RowInvRate=1.39457us
BM_ArrayCountDistinctVarchar/1000/10/40     1417999 ns      1417935 ns          494 RowInvRate=1.41793us
BM_ArrayCountDistinctVarchar/10000/10/40   14167084 ns     14165885 ns           49 RowInvRate=1.41659us
BM_ArrayCountDistinctVarchar/1000/50/40      954943 ns       954887 ns          730 RowInvRate=954.887ns
BM_ArrayCountDistinctVarchar/10000/50/40    9514308 ns      9513680 ns           74 RowInvRate=951.368ns
BM_ArrayCountDistinctVarchar/1000/0/80      2558225 ns      2558090 ns          274 RowInvRate=2.55809us
BM_ArrayCountDistinctVarchar/10000/0/80    26488270 ns     26487371 ns           26 RowInvRate=2.64874us
BM_ArrayCountDistinctVarchar/1000/10/80     2595638 ns      2595522 ns          270 RowInvRate=2.59552us
BM_ArrayCountDistinctVarchar/10000/10/80   26926499 ns     26925436 ns           26 RowInvRate=2.69254us
BM_ArrayCountDistinctVarchar/1000/50/80     1783618 ns      1783545 ns          392 RowInvRate=1.78354us
BM_ArrayCountDistinctVarchar/10000/50/80   18726776 ns     18726294 ns           37 RowInvRate=1.87263us
*/

static void BM_ArrayCountDistinctVarchar(benchmark::State& state) {
    int num_rows = state.range(0);
    double null_probability = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT));
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
        auto result = CelonisArrayCountDistinct<TYPE_VARCHAR>::array_count_distinct(ctx.get(), {input_column});

        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Null percentage / Array length
BENCHMARK(BM_ArrayCountDistinctVarchar)->ArgsProduct({{1000, 10000}, {0, 10, 50}, {20, 40, 80}});

} // namespace starrocks

BENCHMARK_MAIN();
