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
2025-07-26T01:37:51+00:00
Running ./be/build_Release/src/bench/celonis/output/sanitize_invalid_utf8_bench
Run on (32 X 3007.28 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.20, 3.97, 3.41
// Args: Number of rows / String length
----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
BM_SanitizeInvalidUtf8/1000/20/1         60076 ns        60067 ns        11671 RowInvRate=60.0666ns
BM_SanitizeInvalidUtf8/10000/20/1       586147 ns       586086 ns         1206 RowInvRate=58.6086ns
BM_SanitizeInvalidUtf8/100000/20/1     5728477 ns      5728223 ns          122 RowInvRate=57.2822ns
BM_SanitizeInvalidUtf8/1000/40/1         99142 ns        99127 ns         7111 RowInvRate=99.1268ns
BM_SanitizeInvalidUtf8/10000/40/1       959219 ns       959146 ns          732 RowInvRate=95.9146ns
BM_SanitizeInvalidUtf8/100000/40/1     9579353 ns      9579305 ns           73 RowInvRate=95.793ns
BM_SanitizeInvalidUtf8/1000/80/1        176975 ns       176939 ns         4212 RowInvRate=176.939ns
BM_SanitizeInvalidUtf8/10000/80/1      1718491 ns      1718357 ns          400 RowInvRate=171.836ns
BM_SanitizeInvalidUtf8/100000/80/1    17817344 ns     17800834 ns           40 RowInvRate=178.008ns
BM_SanitizeInvalidUtf8/1000/20/5         68408 ns        68394 ns        10224 RowInvRate=68.3941ns
BM_SanitizeInvalidUtf8/10000/20/5       674592 ns       674554 ns         1049 RowInvRate=67.4554ns
BM_SanitizeInvalidUtf8/100000/20/5     6638220 ns      6638131 ns          103 RowInvRate=66.3813ns
BM_SanitizeInvalidUtf8/1000/40/5        111938 ns       111929 ns         6234 RowInvRate=111.929ns
BM_SanitizeInvalidUtf8/10000/40/5      1115152 ns      1115073 ns          637 RowInvRate=111.507ns
BM_SanitizeInvalidUtf8/100000/40/5    11039723 ns     11039655 ns           62 RowInvRate=110.397ns
BM_SanitizeInvalidUtf8/1000/80/5        191622 ns       191608 ns         3651 RowInvRate=191.608ns
BM_SanitizeInvalidUtf8/10000/80/5      1887242 ns      1887188 ns          369 RowInvRate=188.719ns
BM_SanitizeInvalidUtf8/100000/80/5    20040292 ns     20040359 ns           35 RowInvRate=200.404ns
BM_SanitizeInvalidUtf8/1000/20/10        78766 ns        78758 ns         8826 RowInvRate=78.7583ns
BM_SanitizeInvalidUtf8/10000/20/10      768445 ns       768434 ns          909 RowInvRate=76.8434ns
BM_SanitizeInvalidUtf8/100000/20/10    7599789 ns      7599494 ns           91 RowInvRate=75.9949ns
BM_SanitizeInvalidUtf8/1000/40/10       130237 ns       130222 ns         5321 RowInvRate=130.222ns
BM_SanitizeInvalidUtf8/10000/40/10     1286245 ns      1286158 ns          539 RowInvRate=128.616ns
BM_SanitizeInvalidUtf8/100000/40/10   12756650 ns     12756567 ns           55 RowInvRate=127.566ns
BM_SanitizeInvalidUtf8/1000/80/10       225041 ns       225019 ns         3104 RowInvRate=225.019ns
BM_SanitizeInvalidUtf8/10000/80/10     2207885 ns      2207834 ns          315 RowInvRate=220.783ns
BM_SanitizeInvalidUtf8/100000/80/10   22926173 ns     22924741 ns           30 RowInvRate=229.247ns
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
