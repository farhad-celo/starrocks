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
2025-06-10T19:39:01+00:00
Running ./be/build_Release/src/bench/celonis/output/array_count_distinct_bench
Run on (32 X 3401.86 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.89, 5.48, 9.11
// Args: Number of rows / Duplicate rate percentage / Array length
---------------------------------------------------------------------------------------------------
Benchmark                                         Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------
BM_ArrayCountDistinctVarchar/1000/0/20       435112 ns       435119 ns         1610 RowInvRate=435.119ns
BM_ArrayCountDistinctVarchar/10000/0/20     4301370 ns      4301330 ns          162 RowInvRate=430.133ns
BM_ArrayCountDistinctVarchar/1000/25/20      417451 ns       417446 ns         1681 RowInvRate=417.446ns
BM_ArrayCountDistinctVarchar/10000/25/20    4105679 ns      4105515 ns          170 RowInvRate=410.552ns
BM_ArrayCountDistinctVarchar/1000/50/20      351797 ns       351777 ns         1993 RowInvRate=351.777ns
BM_ArrayCountDistinctVarchar/10000/50/20    3485722 ns      3485672 ns          199 RowInvRate=348.567ns
BM_ArrayCountDistinctVarchar/1000/75/20      327216 ns       327201 ns         2131 RowInvRate=327.201ns
BM_ArrayCountDistinctVarchar/10000/75/20    3245173 ns      3245209 ns          216 RowInvRate=324.521ns
BM_ArrayCountDistinctVarchar/1000/0/40       787955 ns       787938 ns          892 RowInvRate=787.938ns
BM_ArrayCountDistinctVarchar/10000/0/40     7845502 ns      7845463 ns           88 RowInvRate=784.546ns
BM_ArrayCountDistinctVarchar/1000/25/40      822796 ns       822784 ns          850 RowInvRate=822.784ns
BM_ArrayCountDistinctVarchar/10000/25/40    8237105 ns      8236975 ns           86 RowInvRate=823.697ns
BM_ArrayCountDistinctVarchar/1000/50/40      806044 ns       806020 ns          867 RowInvRate=806.02ns
BM_ArrayCountDistinctVarchar/10000/50/40    8019104 ns      8018813 ns           86 RowInvRate=801.881ns
BM_ArrayCountDistinctVarchar/1000/75/40      590799 ns       590748 ns         1184 RowInvRate=590.748ns
BM_ArrayCountDistinctVarchar/10000/75/40    5838231 ns      5837914 ns          120 RowInvRate=583.791ns
BM_ArrayCountDistinctVarchar/1000/0/80      1476279 ns      1476242 ns          474 RowInvRate=1.47624us
BM_ArrayCountDistinctVarchar/10000/0/80    15702665 ns     15702620 ns           45 RowInvRate=1.57026us
BM_ArrayCountDistinctVarchar/1000/25/80     1520072 ns      1519984 ns          462 RowInvRate=1.51998us
BM_ArrayCountDistinctVarchar/10000/25/80   16184125 ns     16182771 ns           43 RowInvRate=1.61828us
BM_ArrayCountDistinctVarchar/1000/50/80     1465881 ns      1465810 ns          478 RowInvRate=1.46581us
BM_ArrayCountDistinctVarchar/10000/50/80   15620801 ns     15620754 ns           45 RowInvRate=1.56208us
BM_ArrayCountDistinctVarchar/1000/75/80     1399831 ns      1399779 ns          501 RowInvRate=1.39978us
BM_ArrayCountDistinctVarchar/10000/75/80   14998936 ns     14998300 ns           47 RowInvRate=1.49983us
*/

static void BM_ArrayCountDistinctVarchar(benchmark::State& state) {
    int num_rows = state.range(0);
    double duplicate_rate = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());

    // Calculate the number of unique values based on duplicate rate
    // If duplicate_rate is 0%, all values are unique (array_length unique values)
    // If duplicate_rate is 50%, half the values are duplicates (array_length/2 unique values)
    int num_unique_values = std::max(1, static_cast<int>(array_length * (1.0 - duplicate_rate)));

    std::vector<std::string> unique_strings;
    unique_strings.reserve(num_unique_values);
    for (int j = 0; j < num_unique_values; ++j) {
        unique_strings.emplace_back("value" + std::to_string(j));
    }

    std::uniform_int_distribution<> value_dist(0, num_unique_values - 1);

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
                int value_index = value_dist(gen);
                input_array.emplace_back(Slice(unique_strings[value_index]));
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

// Args: Number of rows / Duplicate rate percentage / Array length
BENCHMARK(BM_ArrayCountDistinctVarchar)->ArgsProduct({{1000, 10000}, {0, 25, 50, 75}, {20, 40, 80}});

} // namespace starrocks

BENCHMARK_MAIN();