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
2025-12-05T18:52:40+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v4_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.81, 2.12, 3.39
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Hash128V4/100000/1/0/10    4127799 ns      4126876 ns          172 RowInvRate=41.2688ns
BM_Hash128V4/100000/2/0/10    6785208 ns      6784584 ns           99 RowInvRate=67.8458ns
BM_Hash128V4/100000/3/0/10    9152392 ns      9151793 ns           77 RowInvRate=91.5179ns
BM_Hash128V4/100000/1/1/10       1049 ns         1001 ns       699796 RowInvRate=555.973ps
BM_Hash128V4/100000/2/1/10    4817159 ns      4816578 ns          144 RowInvRate=48.1658ns
BM_Hash128V4/100000/3/1/10    6964696 ns      6964191 ns           97 RowInvRate=69.6419ns
BM_Hash128V4/100000/1/0/20    4082100 ns      4081246 ns          173 RowInvRate=40.8125ns
BM_Hash128V4/100000/2/0/20    6952918 ns      6951975 ns          103 RowInvRate=69.5198ns
BM_Hash128V4/100000/3/0/20    9355370 ns      9354723 ns           73 RowInvRate=93.5472ns
BM_Hash128V4/100000/1/1/20       1051 ns         1000 ns       699063 RowInvRate=589.275ps
BM_Hash128V4/100000/2/1/20    5039941 ns      5039226 ns          138 RowInvRate=50.3923ns
BM_Hash128V4/100000/3/1/20    7265540 ns      7264899 ns           96 RowInvRate=72.649ns
BM_Hash128V4/100000/1/0/40    4221296 ns      4220737 ns          164 RowInvRate=42.2074ns
BM_Hash128V4/100000/2/0/40    7089152 ns      7086301 ns           99 RowInvRate=70.863ns
BM_Hash128V4/100000/3/0/40    9850761 ns      9849813 ns           74 RowInvRate=98.4981ns
BM_Hash128V4/100000/1/1/40       1064 ns         1016 ns       688537 RowInvRate=5.2096ns
BM_Hash128V4/100000/2/1/40    5201459 ns      5201022 ns          136 RowInvRate=52.0102ns
BM_Hash128V4/100000/3/1/40    7577606 ns      7576971 ns           91 RowInvRate=75.7697ns
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
        ASSERT_TRUE(CelonisStringFunctions::xx_hash3_128_v4(ctx.get(), input_columns).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_Hash128V4(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
BENCHMARK(BM_Hash128V4)->ArgsProduct({{100000}, {1, 2, 3}, {0, 1}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
