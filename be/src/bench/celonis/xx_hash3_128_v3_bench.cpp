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
2025-03-10T18:52:15+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v3_bench
Run on (32 X 3059.04 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.39, 35.04, 32.38
// Args: Number of rows // Number of input columns / Average length of string
-----------------------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------
BM_Hash128V3/100000/1/10   14458689 ns     14456892 ns           49 RowInvRate=144.569ns
BM_Hash128V3/100000/2/10   21501422 ns     21500585 ns           33 RowInvRate=215.006ns
BM_Hash128V3/100000/3/10   27512206 ns     27512185 ns           26 RowInvRate=275.122ns
BM_Hash128V3/100000/1/20   14442961 ns     14442109 ns           49 RowInvRate=144.421ns
BM_Hash128V3/100000/2/20   22295366 ns     22295343 ns           32 RowInvRate=222.953ns
BM_Hash128V3/100000/3/20   37381941 ns     37379237 ns           18 RowInvRate=373.792ns
BM_Hash128V3/100000/1/40   14471988 ns     14471737 ns           48 RowInvRate=144.717ns
BM_Hash128V3/100000/2/40   32621872 ns     32618017 ns           21 RowInvRate=326.18ns
BM_Hash128V3/100000/3/40   47778992 ns     47778370 ns           15 RowInvRate=477.784ns
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
    int avg_length = state.range(2);

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
            for (int i = 0; i < num_rows; i++) {
                input_column->append_datum(Slice(generate_random_string(min_length, max_length)));
            }
            input_columns.push_back(input_column);
        }
       ctx->set_constant_columns({nullptr});

        state.ResumeTiming();
        ASSERT_TRUE(CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), input_columns).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_Hash128V3(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows // Number of input columns / Average length of string
BENCHMARK(BM_Hash128V3)->ArgsProduct({{100000}, {1, 2, 3}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
