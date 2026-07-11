#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-12-05T18:31:39+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v3_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 16.33, 11.40, 7.54
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Hash128V3/100000/1/0/10    5682519 ns      5681413 ns          127 RowInvRate=56.8141ns
BM_Hash128V3/100000/2/0/10    8850294 ns      8849227 ns           79 RowInvRate=88.4923ns
BM_Hash128V3/100000/3/0/10   12411098 ns     12409874 ns           57 RowInvRate=124.099ns
BM_Hash128V3/100000/1/1/10       1077 ns         1031 ns       678634 RowInvRate=-817.332ps
BM_Hash128V3/100000/2/1/10    5645294 ns      5644302 ns          123 RowInvRate=56.443ns
BM_Hash128V3/100000/3/1/10    8958231 ns      8957218 ns           78 RowInvRate=89.5722ns
BM_Hash128V3/100000/1/0/20    5470627 ns      5469709 ns          130 RowInvRate=54.6971ns
BM_Hash128V3/100000/2/0/20    8743499 ns      8742591 ns           81 RowInvRate=87.4259ns
BM_Hash128V3/100000/3/0/20   12162742 ns     12162227 ns           58 RowInvRate=121.622ns
BM_Hash128V3/100000/1/1/20       1080 ns         1034 ns       674695 RowInvRate=-558.195ps
BM_Hash128V3/100000/2/1/20    5730359 ns      5729755 ns          124 RowInvRate=57.2975ns
BM_Hash128V3/100000/3/1/20    8831487 ns      8831046 ns           80 RowInvRate=88.3105ns
BM_Hash128V3/100000/1/0/40    5652252 ns      5651479 ns          129 RowInvRate=56.5148ns
BM_Hash128V3/100000/2/0/40    8320494 ns      8319490 ns           76 RowInvRate=83.1949ns
BM_Hash128V3/100000/3/0/40   11831561 ns     11831040 ns           59 RowInvRate=118.31ns
BM_Hash128V3/100000/1/1/40       1090 ns         1042 ns       672241 RowInvRate=-468.589ps
BM_Hash128V3/100000/2/1/40    5760689 ns      5759685 ns          123 RowInvRate=57.5969ns
BM_Hash128V3/100000/3/1/40    8920463 ns      8919398 ns           79 RowInvRate=89.194ns
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
        arg_types.push_back(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    }
    auto return_type = TypeDescriptor::from_logical_type(TYPE_LARGEINT);
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
