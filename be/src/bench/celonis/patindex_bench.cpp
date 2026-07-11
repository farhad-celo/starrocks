#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/celonis/patindex.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-03-30T17:27:10+00:00
Running ./be/build_Release/src/bench/celonis/output/patindex_bench
Run on (32 X 3242.08 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.04, 3.22, 2.28
----------------------------------------------------------------------------------
Benchmark                        Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------
BM_Patindex/1000/20/1        54604 ns        54591 ns        12858 RowInvRate=54.5906ns
BM_Patindex/10000/20/1      531350 ns       531307 ns         1304 RowInvRate=53.1307ns
BM_Patindex/100000/20/1    5233597 ns      5233587 ns          133 RowInvRate=52.3359ns
BM_Patindex/1000/40/1        57197 ns        57175 ns        12264 RowInvRate=57.1749ns
BM_Patindex/10000/40/1      560195 ns       560193 ns         1234 RowInvRate=56.0193ns
BM_Patindex/100000/40/1    5561512 ns      5561430 ns          125 RowInvRate=55.6143ns
BM_Patindex/1000/20/2        53999 ns        53987 ns        13050 RowInvRate=53.9869ns
BM_Patindex/10000/20/2      531035 ns       531027 ns         1320 RowInvRate=53.1027ns
BM_Patindex/100000/20/2    5259081 ns      5258995 ns          133 RowInvRate=52.59ns
BM_Patindex/1000/40/2        57279 ns        57265 ns        12238 RowInvRate=57.2651ns
BM_Patindex/10000/40/2      564424 ns       564407 ns         1244 RowInvRate=56.4407ns
BM_Patindex/100000/40/2    5884989 ns      5884508 ns          125 RowInvRate=58.8451ns
*/

std::string gen_random_str(int min_length, int max_length) {
    DCHECK_GE(max_length, min_length);
    if (min_length == 0) {
        return "";
    }
    static std::string alphanum =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> length_dist(min_length, max_length);
    std::uniform_int_distribution<int> char_dist(0, alphanum.size() - 1);

    int str_len = length_dist(gen);
    std::string result;
    result.reserve(str_len);
    for (int i = 0; i < str_len; i++) {
        result += alphanum[char_dist(gen)];
    }
    return result;
}

static void do_bench(benchmark::State& state, const std::string& pattern) {
    int num_rows = state.range(0);
    int str_length = state.range(1);
    int64_t index = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                        TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                        TypeDescriptor::from_logical_type(TYPE_BIGINT)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr pattern_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr index_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        pattern_column->append_datum(pattern.data());
        index_column->append_datum(index);
        pattern_column = ConstColumn::create(pattern_column, num_rows);
        index_column = ConstColumn::create(index_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_random_str(1, 2 * str_length).data());
        }
        ctx->set_constant_columns({nullptr, pattern_column, index_column});
        Columns columns;
        columns.push_back(input_column);
        columns.push_back(pattern_column);
        columns.push_back(index_column);
        state.ResumeTiming();
        ASSERT_OK(CelonisPatindex::prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisPatindex::prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisPatindex::patindex(ctx.get(), columns).ok());
        ASSERT_OK(CelonisPatindex::close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisPatindex::close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_Patindex(benchmark::State& state) {
    do_bench(state, "0%");
}

// Args: Number of rows / String length / Index
BENCHMARK(BM_Patindex)->ArgsProduct({{1000, 10000, 100000}, {20, 40}, {1, 2}});

} // namespace starrocks

BENCHMARK_MAIN();
