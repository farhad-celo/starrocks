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
2025-06-08T11:40:05+00:00
Running ./be/build_Release/src/bench/celonis/output/sanitize_invalid_utf8_bench
Run on (32 X 3244.19 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.52, 4.61, 4.79
// Args: Number of rows / String length
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
BM_SanitizeInvalidUtf8/1000/20/1         71922 ns        71919 ns         9762 RowInvRate=71.9186ns
BM_SanitizeInvalidUtf8/10000/20/1       697050 ns       697024 ns         1003 RowInvRate=69.7024ns
BM_SanitizeInvalidUtf8/100000/20/1     6915695 ns      6915672 ns           99 RowInvRate=69.1567ns
BM_SanitizeInvalidUtf8/1000/40/1        121150 ns       121151 ns         5786 RowInvRate=121.151ns
BM_SanitizeInvalidUtf8/10000/40/1      1196731 ns      1196668 ns          583 RowInvRate=119.667ns
BM_SanitizeInvalidUtf8/100000/40/1    11820732 ns     11820279 ns           59 RowInvRate=118.203ns
BM_SanitizeInvalidUtf8/1000/80/1        227429 ns       227451 ns         3071 RowInvRate=227.451ns
BM_SanitizeInvalidUtf8/10000/80/1      2059454 ns      2059367 ns          339 RowInvRate=205.937ns
BM_SanitizeInvalidUtf8/100000/80/1    21581186 ns     21581305 ns           33 RowInvRate=215.813ns
BM_SanitizeInvalidUtf8/1000/20/5         80279 ns        80271 ns         8709 RowInvRate=80.271ns
BM_SanitizeInvalidUtf8/10000/20/5       787381 ns       787394 ns          887 RowInvRate=78.7394ns
BM_SanitizeInvalidUtf8/100000/20/5     7780602 ns      7780416 ns           89 RowInvRate=77.8042ns
BM_SanitizeInvalidUtf8/1000/40/5        135014 ns       135022 ns         5156 RowInvRate=135.022ns
BM_SanitizeInvalidUtf8/10000/40/5      1336579 ns      1336609 ns          525 RowInvRate=133.661ns
BM_SanitizeInvalidUtf8/100000/40/5    13303045 ns     13302801 ns           52 RowInvRate=133.028ns
BM_SanitizeInvalidUtf8/1000/80/5        251219 ns       251235 ns         2869 RowInvRate=251.235ns
BM_SanitizeInvalidUtf8/10000/80/5      2337578 ns      2337552 ns          297 RowInvRate=233.755ns
BM_SanitizeInvalidUtf8/100000/80/5    24003093 ns     24002767 ns           29 RowInvRate=240.028ns
BM_SanitizeInvalidUtf8/1000/20/10        89473 ns        89468 ns         7840 RowInvRate=89.4677ns
BM_SanitizeInvalidUtf8/10000/20/10      876011 ns       876020 ns          801 RowInvRate=87.602ns
BM_SanitizeInvalidUtf8/100000/20/10    8637638 ns      8637405 ns           81 RowInvRate=86.374ns
BM_SanitizeInvalidUtf8/1000/40/10       151274 ns       151282 ns         4600 RowInvRate=151.282ns
BM_SanitizeInvalidUtf8/10000/40/10     1487582 ns      1487604 ns          474 RowInvRate=148.76ns
BM_SanitizeInvalidUtf8/100000/40/10   14814113 ns     14813894 ns           48 RowInvRate=148.139ns
BM_SanitizeInvalidUtf8/1000/80/10       281115 ns       281131 ns         2502 RowInvRate=281.131ns
BM_SanitizeInvalidUtf8/10000/80/10     2688775 ns      2688815 ns          261 RowInvRate=268.881ns
BM_SanitizeInvalidUtf8/100000/80/10   27128624 ns     27128803 ns           25 RowInvRate=271.288ns
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
