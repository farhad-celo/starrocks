#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-04-04T16:46:46+00:00
Running ./be/build_Release/src/bench/celonis/output/sanitize_invalid_utf8_bench
Run on (32 X 3227.72 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.47, 4.38, 6.99
// Args: Number of rows / String length
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
BM_SanitizeInvalidUtf8/1000/20/1         83174 ns        83166 ns         8414 RowInvRate=83.1664ns
BM_SanitizeInvalidUtf8/10000/20/1       813056 ns       813044 ns          859 RowInvRate=81.3044ns
BM_SanitizeInvalidUtf8/100000/20/1     8066145 ns      8065946 ns           86 RowInvRate=80.6595ns
BM_SanitizeInvalidUtf8/1000/40/1        146957 ns       146945 ns         4763 RowInvRate=146.945ns
BM_SanitizeInvalidUtf8/10000/40/1      1449319 ns      1449196 ns          483 RowInvRate=144.92ns
BM_SanitizeInvalidUtf8/100000/40/1    14480812 ns     14479489 ns           48 RowInvRate=144.795ns
BM_SanitizeInvalidUtf8/1000/80/1        268606 ns       268584 ns         2593 RowInvRate=268.584ns
BM_SanitizeInvalidUtf8/10000/80/1      2681553 ns      2681375 ns          263 RowInvRate=268.138ns
BM_SanitizeInvalidUtf8/100000/80/1    27463953 ns     27463755 ns           25 RowInvRate=274.638ns
BM_SanitizeInvalidUtf8/1000/20/5         88812 ns        88803 ns         7862 RowInvRate=88.8029ns
BM_SanitizeInvalidUtf8/10000/20/5       874203 ns       874156 ns          804 RowInvRate=87.4156ns
BM_SanitizeInvalidUtf8/100000/20/5     8632916 ns      8632470 ns           77 RowInvRate=86.3247ns
BM_SanitizeInvalidUtf8/1000/40/5        153916 ns       153902 ns         4566 RowInvRate=153.902ns
BM_SanitizeInvalidUtf8/10000/40/5      1532324 ns      1532297 ns          459 RowInvRate=153.23ns
BM_SanitizeInvalidUtf8/100000/40/5    15109319 ns     15108140 ns           46 RowInvRate=151.081ns
BM_SanitizeInvalidUtf8/1000/80/5        276962 ns       276966 ns         2529 RowInvRate=276.966ns
BM_SanitizeInvalidUtf8/10000/80/5      2776253 ns      2776089 ns          254 RowInvRate=277.609ns
BM_SanitizeInvalidUtf8/100000/80/5    28100049 ns     28100453 ns           25 RowInvRate=281.005ns
BM_SanitizeInvalidUtf8/1000/20/10        97181 ns        97172 ns         7214 RowInvRate=97.1725ns
BM_SanitizeInvalidUtf8/10000/20/10      955596 ns       955561 ns          725 RowInvRate=95.5561ns
BM_SanitizeInvalidUtf8/100000/20/10    9415708 ns      9415311 ns           74 RowInvRate=94.1531ns
BM_SanitizeInvalidUtf8/1000/40/10       168276 ns       168262 ns         4182 RowInvRate=168.262ns
BM_SanitizeInvalidUtf8/10000/40/10     1659597 ns      1659522 ns          423 RowInvRate=165.952ns
BM_SanitizeInvalidUtf8/100000/40/10   16464341 ns     16463768 ns           42 RowInvRate=164.638ns
BM_SanitizeInvalidUtf8/1000/80/10       300564 ns       300546 ns         2332 RowInvRate=300.546ns
BM_SanitizeInvalidUtf8/10000/80/10     2998207 ns      2998128 ns          234 RowInvRate=299.813ns
BM_SanitizeInvalidUtf8/100000/80/10   30617462 ns     30617319 ns           23 RowInvRate=306.173ns
*/

std::string gen_random_str(int min_length, int max_length, int invalid_percentage) {
    DCHECK_GE(max_length, min_length);
    if (min_length == 0) {
        return "";
    }
    static std::string alphanum =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    // These are invalid UTF-8 sequences
    static std::vector<char> invalid_utf8 = {
            static_cast<char>(0xC0),  // Invalid start byte
            static_cast<char>(0xA0),  // Invalid continuation byte
            static_cast<char>(0xFF),  // Invalid in UTF-8
            static_cast<char>(0xFE),  // Invalid in UTF-8
            static_cast<char>(0xC0),  // Overlong encoding start
            static_cast<char>(0x80)   // Continuation byte without start
    };

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> length_dist(min_length, max_length);
    std::uniform_int_distribution<int> char_dist(0, alphanum.size() - 1);
    std::uniform_int_distribution<int> invalid_dist(0, invalid_utf8.size() - 1);
    double invalid_prob = invalid_percentage / 100.0;
    std::bernoulli_distribution dist(invalid_prob);

    int str_len = length_dist(gen);
    std::string result;
    result.reserve(str_len);
    for (int i = 0; i < str_len; i++) {
        bool is_invalid = dist(gen);
        if (is_invalid) {
            result += invalid_utf8[invalid_dist(gen)];
        } else {
            result += alphanum[char_dist(gen)];
        }
    }
    return result;
}

static void do_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int str_length = state.range(1);
    int invalid_percentage = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _: state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_random_str(1, 2 * str_length, invalid_percentage).data());
        }
        ctx->set_constant_columns({nullptr});
        Columns columns;
        columns.push_back(input_column);
        state.ResumeTiming();
        EXPECT_TRUE(CelonisStringFunctions::sanitize_invalid_utf8(ctx.get(), columns).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_SanitizeInvalidUtf8(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / String length / Invalid char percentage
BENCHMARK(BM_SanitizeInvalidUtf8)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 80}, {1, 5, 10}});

} // namespace starrocks

BENCHMARK_MAIN();
