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
2025-12-05T19:35:37+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_96_bench
Run on (32 X 3244.13 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.02, 3.08, 5.91
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
----------------------------------------------------------------------------------
Benchmark                        Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------
BM_Hash96/100000/1/0/10    6800582 ns      6800066 ns          106 RowInvRate=68.0007ns
BM_Hash96/100000/2/0/10    9881340 ns      9880866 ns           72 RowInvRate=98.8087ns
BM_Hash96/100000/3/0/10   13067464 ns     13066897 ns           52 RowInvRate=130.669ns
BM_Hash96/100000/1/1/10       1169 ns         1115 ns       628001 RowInvRate=-430.879ps
BM_Hash96/100000/2/1/10    6901427 ns      6901016 ns          105 RowInvRate=69.0102ns
BM_Hash96/100000/3/1/10   10121654 ns     10121018 ns           70 RowInvRate=101.21ns
BM_Hash96/100000/1/0/20    6660210 ns      6659852 ns          106 RowInvRate=66.5985ns
BM_Hash96/100000/2/0/20    9774121 ns      9773223 ns           71 RowInvRate=97.7322ns
BM_Hash96/100000/3/0/20   13222227 ns     13220909 ns           55 RowInvRate=132.209ns
BM_Hash96/100000/1/1/20       1164 ns         1115 ns       629585 RowInvRate=-478.8ps
BM_Hash96/100000/2/1/20    6860230 ns      6859726 ns          102 RowInvRate=68.5973ns
BM_Hash96/100000/3/1/20    9896943 ns      9895620 ns           71 RowInvRate=98.9562ns
BM_Hash96/100000/1/0/40    6585758 ns      6584687 ns          107 RowInvRate=65.8469ns
BM_Hash96/100000/2/0/40    9293050 ns      9292120 ns           76 RowInvRate=92.9212ns
BM_Hash96/100000/3/0/40   13008269 ns     13007184 ns           54 RowInvRate=130.072ns
BM_Hash96/100000/1/1/40       1173 ns         1121 ns       626322 RowInvRate=-391.563ps
BM_Hash96/100000/2/1/40    6812948 ns      6812303 ns          104 RowInvRate=68.123ns
BM_Hash96/100000/3/1/40    9911898 ns      9911345 ns           70 RowInvRate=99.1135ns
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
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
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
        ASSERT_TRUE(CelonisStringFunctions::xx_hash3_96(ctx.get(), input_columns).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_Hash96(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
BENCHMARK(BM_Hash96)->ArgsProduct({{100000}, {1, 2, 3}, {0, 1}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
