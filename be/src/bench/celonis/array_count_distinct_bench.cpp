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
2025-03-14T18:44:27+00:00
Running ./be/build_Release/src/bench/celonis/output/array_count_distinct_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.04, 4.21, 3.17
// Args: Number of rows / Null percentage / Array length
---------------------------------------------------------------------------------------------------
Benchmark                                         Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------
BM_ArrayCountDistinctVarchar/1000/0/20       306468 ns       306484 ns         2285 RowInvRate=306.484ns
BM_ArrayCountDistinctVarchar/10000/0/20     3029414 ns      3029271 ns          234 RowInvRate=302.927ns
BM_ArrayCountDistinctVarchar/1000/10/20      304771 ns       304761 ns         2295 RowInvRate=304.761ns
BM_ArrayCountDistinctVarchar/10000/10/20    3022134 ns      3022003 ns          232 RowInvRate=302.2ns
BM_ArrayCountDistinctVarchar/1000/50/20      275750 ns       275698 ns         2297 RowInvRate=275.698ns
BM_ArrayCountDistinctVarchar/10000/50/20    2606932 ns      2606719 ns          267 RowInvRate=260.672ns
BM_ArrayCountDistinctVarchar/1000/0/40       577666 ns       577644 ns         1216 RowInvRate=577.644ns
BM_ArrayCountDistinctVarchar/10000/0/40     5670657 ns      5670353 ns          123 RowInvRate=567.035ns
BM_ArrayCountDistinctVarchar/1000/10/40      572252 ns       572159 ns         1233 RowInvRate=572.159ns
BM_ArrayCountDistinctVarchar/10000/10/40    5651476 ns      5651027 ns          124 RowInvRate=565.103ns
BM_ArrayCountDistinctVarchar/1000/50/40      500755 ns       500755 ns         1000 RowInvRate=500.755ns
BM_ArrayCountDistinctVarchar/10000/50/40    4890991 ns      4890760 ns          135 RowInvRate=489.076ns
BM_ArrayCountDistinctVarchar/1000/0/80      1145877 ns      1145818 ns          607 RowInvRate=1.14582us
BM_ArrayCountDistinctVarchar/10000/0/80    12945317 ns     12944693 ns           56 RowInvRate=1.29447us
BM_ArrayCountDistinctVarchar/1000/10/80     1284064 ns      1283786 ns          567 RowInvRate=1.28379us
BM_ArrayCountDistinctVarchar/10000/10/80   13459372 ns     13452945 ns           56 RowInvRate=1.34529us
BM_ArrayCountDistinctVarchar/1000/50/80     1138478 ns      1138114 ns          535 RowInvRate=1.13811us
BM_ArrayCountDistinctVarchar/10000/50/80   10783511 ns     10782942 ns           64 RowInvRate=1078.29ns
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
