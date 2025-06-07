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
2025-06-01T18:07:04+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_nullable_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.00, 1.45, 2.08
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_Hash128Nullable/100000/1/0/10   10580006 ns     10579602 ns           65 RowInvRate=105.796ns
BM_Hash128Nullable/100000/2/0/10   14642489 ns     14642351 ns           48 RowInvRate=146.424ns
BM_Hash128Nullable/100000/3/0/10   17398824 ns     17398578 ns           39 RowInvRate=173.986ns
BM_Hash128Nullable/100000/1/1/10   10131985 ns     10131711 ns           69 RowInvRate=101.317ns
BM_Hash128Nullable/100000/2/1/10   13215027 ns     13215497 ns           53 RowInvRate=132.155ns
BM_Hash128Nullable/100000/3/1/10   16764108 ns     16762572 ns           43 RowInvRate=167.626ns
BM_Hash128Nullable/100000/1/0/20   10374001 ns     10374369 ns           66 RowInvRate=103.744ns
BM_Hash128Nullable/100000/2/0/20   15079872 ns     15080289 ns           46 RowInvRate=150.803ns
BM_Hash128Nullable/100000/3/0/20   23283782 ns     23282896 ns           30 RowInvRate=232.829ns
BM_Hash128Nullable/100000/1/1/20    9998220 ns      9997966 ns           70 RowInvRate=99.9797ns
BM_Hash128Nullable/100000/2/1/20   12967104 ns     12966076 ns           54 RowInvRate=129.661ns
BM_Hash128Nullable/100000/3/1/20   21495782 ns     21495546 ns           33 RowInvRate=214.955ns
BM_Hash128Nullable/100000/1/0/40   10656655 ns     10656830 ns           66 RowInvRate=106.568ns
BM_Hash128Nullable/100000/2/0/40   19496906 ns     19496716 ns           35 RowInvRate=194.967ns
BM_Hash128Nullable/100000/3/0/40   27711832 ns     27710083 ns           25 RowInvRate=277.101ns
BM_Hash128Nullable/100000/1/1/40   10173536 ns     10173491 ns           69 RowInvRate=101.735ns
BM_Hash128Nullable/100000/2/1/40   18318052 ns     18315075 ns           38 RowInvRate=183.151ns
BM_Hash128Nullable/100000/3/1/40   25055003 ns     25055371 ns           28 RowInvRate=250.554ns
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
