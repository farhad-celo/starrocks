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
2025-04-28T16:57:19+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_96_bench
Run on (32 X 3244.62 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.58, 6.53, 7.46
----------------------------------------------------------------------------------
Benchmark                        Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------
BM_Hash96/100000/1/0/10   12437144 ns     12436848 ns           56 RowInvRate=124.368ns
BM_Hash96/100000/2/0/10   17248118 ns     17248316 ns           41 RowInvRate=172.483ns
BM_Hash96/100000/3/0/10   21788993 ns     21789297 ns           32 RowInvRate=217.893ns
BM_Hash96/100000/1/1/10       1273 ns         1258 ns       556957 RowInvRate=-5.04602ns
BM_Hash96/100000/2/1/10   12750760 ns     12747470 ns           56 RowInvRate=127.475ns
BM_Hash96/100000/3/1/10   18387872 ns     18385718 ns           39 RowInvRate=183.857ns
BM_Hash96/100000/1/0/20   12559141 ns     12558916 ns           57 RowInvRate=125.589ns
BM_Hash96/100000/2/0/20   18549279 ns     18549219 ns           38 RowInvRate=185.492ns
BM_Hash96/100000/3/0/20   30385694 ns     30378514 ns           24 RowInvRate=303.785ns
BM_Hash96/100000/1/1/20       1273 ns         1259 ns       554204 RowInvRate=-1.68474ns
BM_Hash96/100000/2/1/20   12654021 ns     12653470 ns           55 RowInvRate=126.535ns
BM_Hash96/100000/3/1/20   24602258 ns     24601940 ns           28 RowInvRate=246.019ns
BM_Hash96/100000/1/0/40   12449878 ns     12450266 ns           56 RowInvRate=124.503ns
BM_Hash96/100000/2/0/40   27704577 ns     27703737 ns           25 RowInvRate=277.037ns
BM_Hash96/100000/3/0/40   37222763 ns     37219681 ns           19 RowInvRate=372.197ns
BM_Hash96/100000/1/1/40       1288 ns         1273 ns       552653 RowInvRate=-1.23584ns
BM_Hash96/100000/2/1/40   22614086 ns     22611003 ns           30 RowInvRate=226.11ns
BM_Hash96/100000/3/1/40   31846300 ns     31841324 ns           22 RowInvRate=318.413ns
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
