#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-11-26T18:47:45+00:00
Running ./be/build_Release/src/bench/celonis/output/translate_bench
Run on (32 X 3246.79 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.92, 6.37, 3.49
// Args: Number of rows / String length
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_TranslateAsciiOnly/1000/20        249351 ns       249343 ns         2809 RowInvRate=249.343ns
BM_TranslateAsciiOnly/10000/20      2460837 ns      2460687 ns          285 RowInvRate=246.069ns
BM_TranslateAsciiOnly/100000/20    24446033 ns     24444854 ns           28 RowInvRate=244.449ns
BM_TranslateAsciiOnly/1000/40        461928 ns       461918 ns         1515 RowInvRate=461.918ns
BM_TranslateAsciiOnly/10000/40      4570476 ns      4570350 ns          153 RowInvRate=457.035ns
BM_TranslateAsciiOnly/100000/40    45788666 ns     45788078 ns           15 RowInvRate=457.881ns
BM_TranslateCommonCase/1000/20       242655 ns       242652 ns         2886 RowInvRate=242.652ns
BM_TranslateCommonCase/10000/20     2393795 ns      2393815 ns          293 RowInvRate=239.382ns
BM_TranslateCommonCase/100000/20   23816225 ns     23815505 ns           29 RowInvRate=238.155ns
BM_TranslateCommonCase/1000/40       450953 ns       450953 ns         1554 RowInvRate=450.953ns
BM_TranslateCommonCase/10000/40     4448631 ns      4448506 ns          157 RowInvRate=444.851ns
BM_TranslateCommonCase/100000/40   44453946 ns     44450884 ns           16 RowInvRate=444.509ns
*/

std::string gen_random_str(int min_length, int max_length) {
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

static void do_bench(benchmark::State& state, const std::string& pattern, const std::string& replace) {
    int num_rows = state.range(0);
    int str_length = state.range(1);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr pattern_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr replace_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        pattern_column->append_datum(pattern.data());
        replace_column->append_datum(replace.data());
        pattern_column = ConstColumn::create(pattern_column, num_rows);
        replace_column = ConstColumn::create(replace_column, num_rows);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_random_str(1, 2 * str_length).data());
        }
        ctx->set_constant_columns({nullptr, pattern_column, replace_column});
        Columns columns;
        columns.push_back(input_column);
        columns.push_back(pattern_column);
        columns.push_back(replace_column);

        state.ResumeTiming();
        ASSERT_OK(CelonisStringFunctions::translate_prepare(ctx.get(),
                                                            FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisStringFunctions::translate_prepare(ctx.get(),
                                                            FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisStringFunctions::translate(ctx.get(), columns).ok());
        ASSERT_OK(
                CelonisStringFunctions::translate_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisStringFunctions::translate_close(ctx.get(),
                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_TranslateAsciiOnly(benchmark::State& state) {
    do_bench(state, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz");
}

static void BM_TranslateCommonCase(benchmark::State& state) {
    do_bench(state, "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ", "abcdefghijklmnopqrstuvwxyzäöü");
}

// Args: Number of rows / String length
BENCHMARK(BM_TranslateAsciiOnly)->ArgsProduct({{1000, 10000, 100000}, {20, 40}});
BENCHMARK(BM_TranslateCommonCase)->ArgsProduct({{1000, 10000, 100000}, {20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
