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
2025-04-05T01:10:31+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v3_bench
Run on (32 X 3241.5 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.09, 7.05, 5.13
// Args: Number of rows / Number of input columns / Number of leading constant columns / Average length of string
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Hash128V3/100000/1/0/10   11736158 ns     11734488 ns           62 RowInvRate=117.345ns
BM_Hash128V3/100000/2/0/10   19823743 ns     19823487 ns           37 RowInvRate=198.235ns
BM_Hash128V3/100000/3/0/10   21857086 ns     21856227 ns           31 RowInvRate=218.562ns
BM_Hash128V3/100000/1/1/10   10896280 ns     10897017 ns           64 RowInvRate=108.97ns
BM_Hash128V3/100000/2/1/10   15046540 ns     15046172 ns           48 RowInvRate=150.462ns
BM_Hash128V3/100000/3/1/10   19433124 ns     19433226 ns           36 RowInvRate=194.332ns
BM_Hash128V3/100000/1/0/20   11920554 ns     11920901 ns           59 RowInvRate=119.209ns
BM_Hash128V3/100000/2/0/20   17715096 ns     17714928 ns           40 RowInvRate=177.149ns
BM_Hash128V3/100000/3/0/20   33940065 ns     33938675 ns           21 RowInvRate=339.387ns
BM_Hash128V3/100000/1/1/20   11436854 ns     11437395 ns           64 RowInvRate=114.374ns
BM_Hash128V3/100000/2/1/20   14989199 ns     14970764 ns           46 RowInvRate=149.708ns
BM_Hash128V3/100000/3/1/20   25723661 ns     25723762 ns           26 RowInvRate=257.238ns
BM_Hash128V3/100000/1/0/40   11614352 ns     11613671 ns           61 RowInvRate=116.137ns
BM_Hash128V3/100000/2/0/40   28559150 ns     28559129 ns           28 RowInvRate=285.591ns
BM_Hash128V3/100000/3/0/40   36507556 ns     36508175 ns           18 RowInvRate=365.082ns
BM_Hash128V3/100000/1/1/40   14081614 ns     14080683 ns           64 RowInvRate=140.807ns
BM_Hash128V3/100000/2/1/40   30114178 ns     30110763 ns           25 RowInvRate=301.108ns
BM_Hash128V3/100000/3/1/40   36557612 ns     36553197 ns           17 RowInvRate=365.532ns
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
