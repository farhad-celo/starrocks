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
2025-03-30T02:22:31+00:00
Running ./be/build_Release/src/bench/celonis/output/translate_bench
Run on (32 X 3252.17 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.37, 4.05, 4.11
// Args: Number of rows / String length
---------------------------------------------------------------------------------
Benchmark                       Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------
BM_Translate/1000/20       277259 ns       277251 ns         2518 RowInvRate=277.251ns
BM_Translate/10000/20     2734183 ns      2734098 ns          256 RowInvRate=273.41ns
BM_Translate/100000/20   27245094 ns     27244463 ns           26 RowInvRate=272.445ns
BM_Translate/1000/40       500306 ns       500310 ns         1403 RowInvRate=500.31ns
BM_Translate/10000/40     4951436 ns      4951484 ns          141 RowInvRate=495.148ns
BM_Translate/100000/40   49572396 ns     49572604 ns           14 RowInvRate=495.726ns
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
    for (auto _: state) {
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
        ASSERT_OK(
                CelonisStringFunctions::translate_prepare(ctx.get(),
                                                          FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisStringFunctions::translate(ctx.get(), columns).ok());
        ASSERT_OK(
                CelonisStringFunctions::translate_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(
                CelonisStringFunctions::translate_close(ctx.get(),
                                                        FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_TranslateCommonCase(benchmark::State& state) {
    do_bench(state, "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ", "abcdefghijklmnopqrstuvwxyzäöü");
}

// Args: Number of rows / String length
BENCHMARK(BM_TranslateCommonCase)->ArgsProduct({{1000, 10000, 100000}, {20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
