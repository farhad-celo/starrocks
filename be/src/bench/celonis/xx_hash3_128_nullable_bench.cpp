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
2025-05-03T12:02:55+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_nullable_bench
Run on (32 X 3089.61 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.08, 2.41, 1.52
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_Hash128Nullable/100000/1/0/10   14006027 ns     14004361 ns           50 RowInvRate=140.044ns
BM_Hash128Nullable/100000/2/0/10   20927605 ns     20927619 ns           32 RowInvRate=209.276ns
BM_Hash128Nullable/100000/3/0/10   27494495 ns     27493880 ns           26 RowInvRate=274.939ns
BM_Hash128Nullable/100000/1/1/10   12912301 ns     12913102 ns           54 RowInvRate=129.131ns
BM_Hash128Nullable/100000/2/1/10   18950599 ns     18949808 ns           37 RowInvRate=189.498ns
BM_Hash128Nullable/100000/3/1/10   25430379 ns     25426446 ns           28 RowInvRate=254.264ns
BM_Hash128Nullable/100000/1/0/20   18863497 ns     18863915 ns           37 RowInvRate=188.639ns
BM_Hash128Nullable/100000/2/0/20   28218059 ns     28218173 ns           25 RowInvRate=282.182ns
BM_Hash128Nullable/100000/3/0/20   50481833 ns     50482011 ns           10 RowInvRate=504.82ns
BM_Hash128Nullable/100000/1/1/20   14875658 ns     14876538 ns           45 RowInvRate=148.765ns
BM_Hash128Nullable/100000/2/1/20   25454197 ns     25445450 ns           29 RowInvRate=254.455ns
BM_Hash128Nullable/100000/3/1/20   41138167 ns     41138084 ns           16 RowInvRate=411.381ns
BM_Hash128Nullable/100000/1/0/40   13590605 ns     13590734 ns           52 RowInvRate=135.907ns
BM_Hash128Nullable/100000/2/0/40   29809673 ns     29808597 ns           24 RowInvRate=298.086ns
BM_Hash128Nullable/100000/3/0/40   43068848 ns     43066476 ns           16 RowInvRate=430.665ns
BM_Hash128Nullable/100000/1/1/40   13149782 ns     13150533 ns           53 RowInvRate=131.505ns
BM_Hash128Nullable/100000/2/1/40   29913494 ns     29912380 ns           25 RowInvRate=299.124ns
BM_Hash128Nullable/100000/3/1/40   43908909 ns     43908376 ns           17 RowInvRate=439.084ns
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
