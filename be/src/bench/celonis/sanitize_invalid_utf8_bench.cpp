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
2025-04-02T18:35:13+00:00
Running ./be/build_Release/src/bench/celonis/output/sanitize_invalid_utf8_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.75, 6.60, 4.18
// Args: Number of rows / String length
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
BM_SanitizeInvalidUtf8/1000/20/1        112399 ns       112394 ns         6224 RowInvRate=112.394ns
BM_SanitizeInvalidUtf8/10000/20/1      1106606 ns      1106581 ns          633 RowInvRate=110.658ns
BM_SanitizeInvalidUtf8/100000/20/1    11019647 ns     11019315 ns           63 RowInvRate=110.193ns
BM_SanitizeInvalidUtf8/1000/40/1        174072 ns       174045 ns         3942 RowInvRate=174.045ns
BM_SanitizeInvalidUtf8/10000/40/1      1725805 ns      1725711 ns          407 RowInvRate=172.571ns
BM_SanitizeInvalidUtf8/100000/40/1    17040438 ns     17040031 ns           41 RowInvRate=170.4ns
BM_SanitizeInvalidUtf8/1000/80/1        295450 ns       295433 ns         2369 RowInvRate=295.433ns
BM_SanitizeInvalidUtf8/10000/80/1      2943571 ns      2943413 ns          239 RowInvRate=294.341ns
BM_SanitizeInvalidUtf8/100000/80/1    30115835 ns     30115657 ns           23 RowInvRate=301.157ns
BM_SanitizeInvalidUtf8/1000/20/5        117571 ns       117567 ns         5925 RowInvRate=117.567ns
BM_SanitizeInvalidUtf8/10000/20/5      1165778 ns      1165691 ns          598 RowInvRate=116.569ns
BM_SanitizeInvalidUtf8/100000/20/5    11596375 ns     11596457 ns           60 RowInvRate=115.965ns
BM_SanitizeInvalidUtf8/1000/40/5        179575 ns       179547 ns         3908 RowInvRate=179.547ns
BM_SanitizeInvalidUtf8/10000/40/5      1775792 ns      1775693 ns          398 RowInvRate=177.569ns
BM_SanitizeInvalidUtf8/100000/40/5    17747225 ns     17746862 ns           40 RowInvRate=177.469ns
BM_SanitizeInvalidUtf8/1000/80/5        301107 ns       301079 ns         2316 RowInvRate=301.079ns
BM_SanitizeInvalidUtf8/10000/80/5      3025731 ns      3025631 ns          232 RowInvRate=302.563ns
BM_SanitizeInvalidUtf8/100000/80/5    30571245 ns     30571364 ns           23 RowInvRate=305.714ns
BM_SanitizeInvalidUtf8/1000/20/10       123986 ns       123984 ns         5656 RowInvRate=123.984ns
BM_SanitizeInvalidUtf8/10000/20/10     1229988 ns      1229950 ns          571 RowInvRate=122.995ns
BM_SanitizeInvalidUtf8/100000/20/10   12319084 ns     12318731 ns           56 RowInvRate=123.187ns
BM_SanitizeInvalidUtf8/1000/40/10       192128 ns       192120 ns         3645 RowInvRate=192.12ns
BM_SanitizeInvalidUtf8/10000/40/10     1908553 ns      1908391 ns          369 RowInvRate=190.839ns
BM_SanitizeInvalidUtf8/100000/40/10   19010814 ns     19008904 ns           37 RowInvRate=190.089ns
BM_SanitizeInvalidUtf8/1000/80/10       322578 ns       322563 ns         2167 RowInvRate=322.563ns
BM_SanitizeInvalidUtf8/10000/80/10     3209369 ns      3209355 ns          217 RowInvRate=320.935ns
BM_SanitizeInvalidUtf8/100000/80/10   32810029 ns     32809527 ns           21 RowInvRate=328.095ns
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
