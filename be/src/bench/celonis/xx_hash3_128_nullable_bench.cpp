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
2025-12-07T00:09:44+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_nullable_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.48, 1.26, 1.50
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_Hash128Nullable/100000/1/0/10    5377197 ns      5376808 ns          132 RowInvRate=53.7681ns
BM_Hash128Nullable/100000/2/0/10    8051082 ns      8049906 ns           85 RowInvRate=80.4991ns
BM_Hash128Nullable/100000/3/0/10   10842640 ns     10840953 ns           65 RowInvRate=108.41ns
BM_Hash128Nullable/100000/1/1/10       1067 ns         1018 ns       687665 RowInvRate=14.8923ns
BM_Hash128Nullable/100000/2/1/10    7436706 ns      7435689 ns           92 RowInvRate=74.3569ns
BM_Hash128Nullable/100000/3/1/10   10438099 ns     10437613 ns           68 RowInvRate=104.376ns
BM_Hash128Nullable/100000/1/0/20    5229185 ns      5228661 ns          137 RowInvRate=52.2866ns
BM_Hash128Nullable/100000/2/0/20    7956446 ns      7955943 ns           90 RowInvRate=79.5594ns
BM_Hash128Nullable/100000/3/0/20   10555363 ns     10554543 ns           67 RowInvRate=105.545ns
BM_Hash128Nullable/100000/1/1/20       1070 ns         1019 ns       686255 RowInvRate=-7.44475ns
BM_Hash128Nullable/100000/2/1/20    7459361 ns      7458999 ns           95 RowInvRate=74.59ns
BM_Hash128Nullable/100000/3/1/20   10196828 ns     10196159 ns           69 RowInvRate=101.962ns
BM_Hash128Nullable/100000/1/0/40    5292805 ns      5292527 ns          133 RowInvRate=52.9253ns
BM_Hash128Nullable/100000/2/0/40    7808627 ns      7807849 ns           88 RowInvRate=78.0785ns
BM_Hash128Nullable/100000/3/0/40   10973219 ns     10971891 ns           62 RowInvRate=109.719ns
BM_Hash128Nullable/100000/1/1/40       1071 ns         1023 ns       682157 RowInvRate=-1.38495ns
BM_Hash128Nullable/100000/2/1/40    7544943 ns      7544729 ns           93 RowInvRate=75.4473ns
BM_Hash128Nullable/100000/3/1/40   10638792 ns     10637985 ns           63 RowInvRate=106.38ns
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
        ASSERT_TRUE(CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), input_columns).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_Hash128Nullable(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
BENCHMARK(BM_Hash128Nullable)->ArgsProduct({{100000}, {1, 2, 3}, {0, 1}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
