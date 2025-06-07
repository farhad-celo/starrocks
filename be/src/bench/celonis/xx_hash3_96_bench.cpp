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
2025-06-01T18:17:18+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_96_bench
Run on (32 X 2773.35 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.00, 1.05, 1.53
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
----------------------------------------------------------------------------------
Benchmark                        Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------
BM_Hash96/100000/1/0/10   12025674 ns     12024224 ns           59 RowInvRate=120.242ns
BM_Hash96/100000/2/0/10   16712411 ns     16710367 ns           42 RowInvRate=167.104ns
BM_Hash96/100000/3/0/10   20629523 ns     20629829 ns           34 RowInvRate=206.298ns
BM_Hash96/100000/1/1/10       1193 ns         1177 ns       594352 RowInvRate=-1007.89ps
BM_Hash96/100000/2/1/10   11929764 ns     11928183 ns           58 RowInvRate=119.282ns
BM_Hash96/100000/3/1/10   16225546 ns     16222838 ns           43 RowInvRate=162.228ns
BM_Hash96/100000/1/0/20   11786532 ns     11783934 ns           59 RowInvRate=117.839ns
BM_Hash96/100000/2/0/20   16631104 ns     16629625 ns           42 RowInvRate=166.296ns
BM_Hash96/100000/3/0/20   26085878 ns     26082281 ns           27 RowInvRate=260.823ns
BM_Hash96/100000/1/1/20       1204 ns         1187 ns       588028 RowInvRate=-526.282ps
BM_Hash96/100000/2/1/20   11949981 ns     11949041 ns           58 RowInvRate=119.49ns
BM_Hash96/100000/3/1/20   20972031 ns     20968692 ns           33 RowInvRate=209.687ns
BM_Hash96/100000/1/0/40   11878821 ns     11879353 ns           59 RowInvRate=118.794ns
BM_Hash96/100000/2/0/40   22622793 ns     22620763 ns           31 RowInvRate=226.208ns
BM_Hash96/100000/3/0/40   28866517 ns     28865567 ns           24 RowInvRate=288.656ns
BM_Hash96/100000/1/1/40       1203 ns         1185 ns       593967 RowInvRate=-960.161ps
BM_Hash96/100000/2/1/40   18435479 ns     18434805 ns           38 RowInvRate=184.348ns
BM_Hash96/100000/3/1/40   24006087 ns     24001572 ns           29 RowInvRate=240.016ns
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
