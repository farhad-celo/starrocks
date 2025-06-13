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
2025-06-13T01:42:36+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v4_bench
Run on (32 X 3243.4 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.90, 3.95, 2.09
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Hash128V4/100000/1/0/10   11149177 ns     11149503 ns           62 RowInvRate=111.495ns
BM_Hash128V4/100000/2/0/10   13354220 ns     13353927 ns           55 RowInvRate=133.539ns
BM_Hash128V4/100000/3/0/10   16246572 ns     16246519 ns           42 RowInvRate=162.465ns
BM_Hash128V4/100000/1/1/10       1122 ns         1097 ns       638499 RowInvRate=-1.2185ns
BM_Hash128V4/100000/2/1/10   10677786 ns     10678294 ns           68 RowInvRate=106.783ns
BM_Hash128V4/100000/3/1/10   13839031 ns     13839428 ns           48 RowInvRate=138.394ns
BM_Hash128V4/100000/1/0/20   10944940 ns     10945296 ns           60 RowInvRate=109.453ns
BM_Hash128V4/100000/2/0/20   15267059 ns     15267600 ns           46 RowInvRate=152.676ns
BM_Hash128V4/100000/3/0/20   24836885 ns     24835377 ns           28 RowInvRate=248.354ns
BM_Hash128V4/100000/1/1/20       1122 ns         1095 ns       641233 RowInvRate=-2.3312ns
BM_Hash128V4/100000/2/1/20   11557812 ns     11557636 ns           60 RowInvRate=115.576ns
BM_Hash128V4/100000/3/1/20   19474705 ns     19471856 ns           36 RowInvRate=194.719ns
BM_Hash128V4/100000/1/0/40   11426977 ns     11425742 ns           61 RowInvRate=114.257ns
BM_Hash128V4/100000/2/0/40   20938657 ns     20937849 ns           33 RowInvRate=209.378ns
BM_Hash128V4/100000/3/0/40   28356359 ns     28353724 ns           25 RowInvRate=283.537ns
BM_Hash128V4/100000/1/1/40       1127 ns         1103 ns       638991 RowInvRate=-1.34197ns
BM_Hash128V4/100000/2/1/40   15582952 ns     15582767 ns           44 RowInvRate=155.828ns
BM_Hash128V4/100000/3/1/40   23310111 ns     23310552 ns           29 RowInvRate=233.106ns
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
