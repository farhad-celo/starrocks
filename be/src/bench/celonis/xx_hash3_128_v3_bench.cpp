#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-04-07T19:14:27+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v3_bench
Run on (32 X 3244.01 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.79, 5.26, 5.07
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Hash128V3/100000/1/0/10   11825614 ns     11825942 ns           60 RowInvRate=118.259ns
BM_Hash128V3/100000/2/0/10   16592845 ns     16592172 ns           41 RowInvRate=165.922ns
BM_Hash128V3/100000/3/0/10   20812459 ns     20812602 ns           34 RowInvRate=208.126ns
BM_Hash128V3/100000/1/1/10       1174 ns         1157 ns       606285 RowInvRate=1.40528ns
BM_Hash128V3/100000/2/1/10   11629629 ns     11629605 ns           60 RowInvRate=116.296ns
BM_Hash128V3/100000/3/1/10   16678253 ns     16678202 ns           42 RowInvRate=166.782ns
BM_Hash128V3/100000/1/0/20   11887128 ns     11886963 ns           58 RowInvRate=118.87ns
BM_Hash128V3/100000/2/0/20   18821570 ns     18821796 ns           41 RowInvRate=188.218ns
BM_Hash128V3/100000/3/0/20   35274886 ns     35011830 ns           23 RowInvRate=350.118ns
BM_Hash128V3/100000/1/1/20       2876 ns         2094 ns       311727 RowInvRate=589.157ps
BM_Hash128V3/100000/2/1/20   12212123 ns     12211695 ns           44 RowInvRate=122.117ns
BM_Hash128V3/100000/3/1/20   23068401 ns     23067371 ns           30 RowInvRate=230.674ns
BM_Hash128V3/100000/1/0/40   11615307 ns     11615551 ns           61 RowInvRate=116.156ns
BM_Hash128V3/100000/2/0/40   26409566 ns     26408476 ns           27 RowInvRate=264.085ns
BM_Hash128V3/100000/3/0/40   37707455 ns     37706493 ns           18 RowInvRate=377.065ns
BM_Hash128V3/100000/1/1/40       1173 ns         1156 ns       609094 RowInvRate=902.496ps
BM_Hash128V3/100000/2/1/40   20094013 ns     20093876 ns           35 RowInvRate=200.939ns
BM_Hash128V3/100000/3/1/40   31866130 ns     31864937 ns           22 RowInvRate=318.649ns
*/

std::string generate_random_string(int min_len, int max_len) {
    std::string chars =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937 gen(rd());

    // Generate a random length within the specified range
    std::uniform_int_distribution<> length_distrib(min_len, max_len);
    int len = length_distrib(gen);

    std::uniform_int_distribution<> char_distrib(0, chars.size() - 1);

    std::string random_string;
    for (int i = 0; i < len; ++i) {
        random_string += chars[char_distrib(gen)];
    }
    return random_string;
}

static void do_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_cols = state.range(1);
    int num_const_cols = state.range(2);
    int avg_length = state.range(3);
    DCHECK_GE(num_cols, num_const_cols);

    std::vector<FunctionContext::TypeDesc> arg_types;
    arg_types.reserve(num_cols);
    for (auto i = 0; i < num_cols; ++i) {
        arg_types.push_back(AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)));
    }
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    int min_length = avg_length - int(avg_length * 2 / 10);
    int max_length = avg_length + int(avg_length * 2 / 10);
    starrocks::Columns input_columns;
    for (auto _ : state) {
        state.PauseTiming();
        input_columns.clear();
        input_columns.reserve(num_cols);
        total_rows += num_rows;
        for (auto col = 0; col < num_cols; ++col) {
            auto input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
            if (col < num_const_cols) {
                input_column->append_datum(Slice(generate_random_string(avg_length, avg_length)));
                input_column = ConstColumn::create(input_column, num_rows);
            } else {
                for (int i = 0; i < num_rows; i++) {
                    input_column->append_datum(Slice(generate_random_string(min_length, max_length)));
                }
            }
            input_columns.push_back(input_column);
        }
        starrocks::Columns constant_columns;
        constant_columns.reserve(num_cols);
        for (const auto& input_column : input_columns) {
            constant_columns.push_back(input_column->is_constant() ? input_column : nullptr);
        }
        ctx->set_constant_columns(constant_columns);

        state.ResumeTiming();
        ASSERT_TRUE(CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), input_columns).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_Hash128V3(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
BENCHMARK(BM_Hash128V3)->ArgsProduct({{100000}, {1, 2, 3}, {0, 1}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
