#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "exprs/string_functions.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-08-17T14:35:20+00:00
Running ./be/build_Release/src/bench/celonis/output/replace_deletion_bench
Run on (32 X 3241.38 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.39, 1.95, 1.47
// Args: Number of rows / String length / Pattern length / Pattern count
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_ReplaceDeletion/4096/10/3/1       191963 ns       191807 ns         3806 RowInvRate=46.8279ns
BM_ReplaceDeletion/10000/10/3/1      452543 ns       452495 ns         1536 RowInvRate=45.2495ns
BM_ReplaceDeletion/100000/10/3/1    4528062 ns      4527651 ns          156 RowInvRate=45.2765ns
BM_ReplaceDeletion/4096/20/3/1       249697 ns       249621 ns         2797 RowInvRate=60.9427ns
BM_ReplaceDeletion/10000/20/3/1      601965 ns       601867 ns         1143 RowInvRate=60.1867ns
BM_ReplaceDeletion/100000/20/3/1    6163177 ns      6162684 ns          110 RowInvRate=61.6268ns
BM_ReplaceDeletion/4096/10/5/1       180767 ns       180728 ns         3850 RowInvRate=44.1231ns
BM_ReplaceDeletion/10000/10/5/1      440991 ns       440951 ns         1597 RowInvRate=44.0951ns
BM_ReplaceDeletion/100000/10/5/1    4322128 ns      4321199 ns          161 RowInvRate=43.212ns
BM_ReplaceDeletion/4096/20/5/1       257554 ns       257537 ns         2734 RowInvRate=62.8753ns
BM_ReplaceDeletion/10000/20/5/1      623705 ns       623620 ns         1136 RowInvRate=62.362ns
BM_ReplaceDeletion/100000/20/5/1    6284236 ns      6283685 ns          112 RowInvRate=62.8368ns
BM_ReplaceDeletion/4096/10/3/3       170587 ns       170555 ns         4083 RowInvRate=41.6394ns
BM_ReplaceDeletion/10000/10/3/3      420222 ns       420223 ns         1673 RowInvRate=42.0223ns
BM_ReplaceDeletion/100000/10/3/3    4264518 ns      4264017 ns          155 RowInvRate=42.6402ns
BM_ReplaceDeletion/4096/20/3/3       280873 ns       280847 ns         2488 RowInvRate=68.5661ns
BM_ReplaceDeletion/10000/20/3/3      718621 ns       718458 ns         1001 RowInvRate=71.8458ns
BM_ReplaceDeletion/100000/20/3/3    7214483 ns      7213588 ns           92 RowInvRate=72.1359ns
BM_ReplaceDeletion/4096/10/5/3       201773 ns       201771 ns         3482 RowInvRate=49.2605ns
BM_ReplaceDeletion/10000/10/5/3      495265 ns       495255 ns         1448 RowInvRate=49.5255ns
BM_ReplaceDeletion/100000/10/5/3    5152692 ns      5152156 ns          100 RowInvRate=51.5216ns
BM_ReplaceDeletion/4096/20/5/3       307692 ns       307673 ns         2262 RowInvRate=75.1154ns
BM_ReplaceDeletion/10000/20/5/3      741209 ns       741158 ns          919 RowInvRate=74.1158ns
BM_ReplaceDeletion/100000/20/5/3    7866175 ns      7865483 ns           91 RowInvRate=78.6548ns
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

static void do_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int str_length = state.range(1);
    int pattern_length = state.range(2);
    int pattern_count = state.range(3);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    // Generate a pattern to remove
    static std::string pattern_chars = "ABC123";
    static std::mt19937 pattern_gen(std::random_device{}());
    std::uniform_int_distribution<int> char_dist(0, pattern_chars.size() - 1);

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        std::string pattern;
        pattern.reserve(pattern_length);
        for (int i = 0; i < pattern_length; i++) {
            pattern += pattern_chars[char_dist(pattern_gen)];
        }

        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr pattern_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr replacement_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        pattern_column->append_datum(pattern.data());
        replacement_column->append_datum(""); // Empty replacement for deletion
        pattern_column = ConstColumn::create(pattern_column, num_rows);
        replacement_column = ConstColumn::create(replacement_column, num_rows);

        // Calculate base string length to achieve target length with patterns inserted
        int base_length = str_length - (pattern_count * pattern_length);
        if (base_length < 1) {
            base_length = 1;
        }

        // Generate input strings with patterns to be deleted
        for (int i = 0; i < num_rows; i++) {
            std::string input_str = gen_random_str(base_length / 2, base_length);

            // Insert patterns at random positions
            std::mt19937 pos_gen(std::random_device{}());
            for (int j = 0; j < pattern_count; j++) {
                std::uniform_int_distribution<int> pos_dist(0, input_str.length());
                int pos = pos_dist(pos_gen);
                input_str.insert(pos, pattern);
            }

            input_column->append_datum(input_str.data());
        }

        StringFunctions::replace_prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL);
        ctx->set_constant_columns({nullptr, pattern_column, replacement_column});

        Columns columns;
        columns.push_back(input_column);
        columns.push_back(pattern_column);
        columns.push_back(replacement_column);

        state.ResumeTiming();
        EXPECT_TRUE(StringFunctions::replace(ctx.get(), columns).ok());
        state.PauseTiming();

        StringFunctions::replace_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL);
        state.ResumeTiming();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ReplaceDeletion(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / String length / Pattern length / Pattern count
BENCHMARK(BM_ReplaceDeletion)->ArgsProduct({{4096, 10000, 100000}, {10, 20}, {3, 5}, {1, 3}});

} // namespace starrocks

BENCHMARK_MAIN();
