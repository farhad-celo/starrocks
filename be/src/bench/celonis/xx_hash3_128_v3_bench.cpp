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
2025-06-01T17:56:35+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v3_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.80, 4.58, 3.08
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Hash128V3/100000/1/0/10   11290502 ns     11289838 ns           62 RowInvRate=112.898ns
BM_Hash128V3/100000/2/0/10   15624784 ns     15624509 ns           45 RowInvRate=156.245ns
BM_Hash128V3/100000/3/0/10   19811427 ns     19808722 ns           35 RowInvRate=198.087ns
BM_Hash128V3/100000/1/1/10       1128 ns         1107 ns       632655 RowInvRate=-604.47ps
BM_Hash128V3/100000/2/1/10   11265361 ns     11263897 ns           63 RowInvRate=112.639ns
BM_Hash128V3/100000/3/1/10   15185546 ns     15181303 ns           46 RowInvRate=151.813ns
BM_Hash128V3/100000/1/0/20   10882456 ns     10881736 ns           65 RowInvRate=108.817ns
BM_Hash128V3/100000/2/0/20   15469037 ns     15469440 ns           46 RowInvRate=154.694ns
BM_Hash128V3/100000/3/0/20   24883627 ns     24881873 ns           28 RowInvRate=248.819ns
BM_Hash128V3/100000/1/1/20       1140 ns         1122 ns       624336 RowInvRate=-351.806ps
BM_Hash128V3/100000/2/1/20   10983256 ns     10981973 ns           64 RowInvRate=109.82ns
BM_Hash128V3/100000/3/1/20   19491383 ns     19489845 ns           35 RowInvRate=194.898ns
BM_Hash128V3/100000/1/0/40   10838164 ns     10835519 ns           65 RowInvRate=108.355ns
BM_Hash128V3/100000/2/0/40   20222938 ns     20221025 ns           35 RowInvRate=202.21ns
BM_Hash128V3/100000/3/0/40   28842706 ns     28840032 ns           24 RowInvRate=288.4ns
BM_Hash128V3/100000/1/1/40       1142 ns         1121 ns       623839 RowInvRate=-342.572ps
BM_Hash128V3/100000/2/1/40   16218063 ns     16218312 ns           44 RowInvRate=162.183ns
BM_Hash128V3/100000/3/1/40   23535180 ns     23533470 ns           30 RowInvRate=235.335ns
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
