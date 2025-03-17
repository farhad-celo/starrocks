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
2025-03-10T19:13:00+00:00
Running ./be/build_Release/src/bench/celonis/output/xx_hash3_128_v3_bench
Run on (32 X 3105.45 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.28, 1.76, 9.44
// Args: Number of rows // Number of input columns / Average length of string
-----------------------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------
BM_Hash128V3/100000/1/10   11906416 ns     11906654 ns           60 RowInvRate=119.067ns
BM_Hash128V3/100000/2/10   16754586 ns     16751585 ns           42 RowInvRate=167.516ns
BM_Hash128V3/100000/3/10   20996633 ns     20992097 ns           33 RowInvRate=209.921ns
BM_Hash128V3/100000/1/20   11399903 ns     11399384 ns           61 RowInvRate=113.994ns
BM_Hash128V3/100000/2/20   17770555 ns     17770182 ns           39 RowInvRate=177.702ns
BM_Hash128V3/100000/3/20   28975125 ns     28973272 ns           24 RowInvRate=289.733ns
BM_Hash128V3/100000/1/40   11527241 ns     11527025 ns           61 RowInvRate=115.27ns
BM_Hash128V3/100000/2/40   25774769 ns     25774113 ns           27 RowInvRate=257.741ns
BM_Hash128V3/100000/3/40   36962701 ns     36962418 ns           19 RowInvRate=369.624ns
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
