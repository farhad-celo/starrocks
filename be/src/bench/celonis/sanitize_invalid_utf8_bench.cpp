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
2025-11-22T11:15:41+00:00
Running ./be/build_Release/src/bench/celonis/output/sanitize_invalid_utf8_bench
Run on (32 X 3246.82 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.17, 4.51, 6.76
// Args: Number of rows / String length / Invalid char percentage
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
BM_SanitizeInvalidUtf8/1000/20/0         39045 ns        39043 ns        17940 RowInvRate=39.0427ns
BM_SanitizeInvalidUtf8/10000/20/0       372973 ns       372980 ns         1880 RowInvRate=37.298ns
BM_SanitizeInvalidUtf8/100000/20/0     3698398 ns      3698319 ns          190 RowInvRate=36.9832ns
BM_SanitizeInvalidUtf8/1000/40/0         45300 ns        45303 ns        15482 RowInvRate=45.3032ns
BM_SanitizeInvalidUtf8/10000/40/0       442084 ns       442052 ns         1623 RowInvRate=44.2052ns
BM_SanitizeInvalidUtf8/100000/40/0     4380276 ns      4380008 ns          162 RowInvRate=43.8001ns
BM_SanitizeInvalidUtf8/1000/80/0         52473 ns        52443 ns        13264 RowInvRate=52.4429ns
BM_SanitizeInvalidUtf8/10000/80/0       496227 ns       496258 ns         1345 RowInvRate=49.6258ns
BM_SanitizeInvalidUtf8/100000/80/0     5666403 ns      5666170 ns          123 RowInvRate=56.6617ns
BM_SanitizeInvalidUtf8/1000/20/1         53204 ns        53205 ns        13167 RowInvRate=53.2047ns
BM_SanitizeInvalidUtf8/10000/20/1       514874 ns       514859 ns         1357 RowInvRate=51.4859ns
BM_SanitizeInvalidUtf8/100000/20/1     5085197 ns      5085240 ns          135 RowInvRate=50.8524ns
BM_SanitizeInvalidUtf8/1000/40/1         82508 ns        82519 ns         8478 RowInvRate=82.5189ns
BM_SanitizeInvalidUtf8/10000/40/1       803000 ns       802957 ns          865 RowInvRate=80.2957ns
BM_SanitizeInvalidUtf8/100000/40/1     8000823 ns      8000191 ns           87 RowInvRate=80.0019ns
BM_SanitizeInvalidUtf8/1000/80/1        152442 ns       152444 ns         4609 RowInvRate=152.444ns
BM_SanitizeInvalidUtf8/10000/80/1      1496059 ns      1496009 ns          466 RowInvRate=149.601ns
BM_SanitizeInvalidUtf8/100000/80/1    15814880 ns     15812448 ns           45 RowInvRate=158.124ns
BM_SanitizeInvalidUtf8/1000/20/5         81276 ns        81277 ns         8617 RowInvRate=81.2773ns
BM_SanitizeInvalidUtf8/10000/20/5       791385 ns       791371 ns          886 RowInvRate=79.1371ns
BM_SanitizeInvalidUtf8/100000/20/5     7870340 ns      7869863 ns           88 RowInvRate=78.6986ns
BM_SanitizeInvalidUtf8/1000/40/5        128130 ns       128140 ns         5474 RowInvRate=128.14ns
BM_SanitizeInvalidUtf8/10000/40/5      1262783 ns      1262724 ns          556 RowInvRate=126.272ns
BM_SanitizeInvalidUtf8/100000/40/5    12586515 ns     12586008 ns           55 RowInvRate=125.86ns
BM_SanitizeInvalidUtf8/1000/80/5        216442 ns       216432 ns         3234 RowInvRate=216.432ns
BM_SanitizeInvalidUtf8/10000/80/5      2142574 ns      2142481 ns          324 RowInvRate=214.248ns
BM_SanitizeInvalidUtf8/100000/80/5    22156353 ns     22154647 ns           31 RowInvRate=221.546ns
BM_SanitizeInvalidUtf8/1000/20/10        94008 ns        94007 ns         7438 RowInvRate=94.0073ns
BM_SanitizeInvalidUtf8/10000/20/10      925348 ns       925282 ns          759 RowInvRate=92.5282ns
BM_SanitizeInvalidUtf8/100000/20/10    9152400 ns      9152022 ns           75 RowInvRate=91.5202ns
BM_SanitizeInvalidUtf8/1000/40/10       148041 ns       148030 ns         4727 RowInvRate=148.03ns
BM_SanitizeInvalidUtf8/10000/40/10     1462777 ns      1462671 ns          481 RowInvRate=146.267ns
BM_SanitizeInvalidUtf8/100000/40/10   14593667 ns     14592624 ns           48 RowInvRate=145.926ns
BM_SanitizeInvalidUtf8/1000/80/10       249179 ns       249171 ns         2809 RowInvRate=249.171ns
BM_SanitizeInvalidUtf8/10000/80/10     2462137 ns      2462054 ns          284 RowInvRate=246.205ns
BM_SanitizeInvalidUtf8/100000/80/10   25298698 ns     25296324 ns           27 RowInvRate=252.963ns
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
            static_cast<char>(0xC0), // Invalid start byte
            static_cast<char>(0xA0), // Invalid continuation byte
            static_cast<char>(0xFF), // Invalid in UTF-8
            static_cast<char>(0xFE), // Invalid in UTF-8
            static_cast<char>(0xC0), // Overlong encoding start
            static_cast<char>(0x80)  // Continuation byte without start
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
    for (auto _ : state) {
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
BENCHMARK(BM_SanitizeInvalidUtf8)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 80}, {0, 1, 5, 10}});

} // namespace starrocks

BENCHMARK_MAIN();
