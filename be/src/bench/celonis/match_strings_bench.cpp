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
2024-12-20T03:53:38+00:00
Running ./be/build_Release/src/bench/celonis/output/match_strings_bench
Run on (32 X 2445.43 MHz CPU s)                                                                                                                                      [0/1883]
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.60, 3.47, 3.78
// Args: Number of rows / Number of possible strings / Size of match string / Value of top_k
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MatchStringsNonConstant/1000/20/20/3       4143906 ns      4143790 ns          176 RowInvRate=4.14379us
BM_MatchStringsNonConstant/10000/20/20/3     43346160 ns     43344528 ns           18 RowInvRate=4.33445us
BM_MatchStringsNonConstant/100000/20/20/3   383505569 ns    383491326 ns            2 RowInvRate=3.83491us
BM_MatchStringsNonConstant/1000/40/20/3       4758092 ns      4757890 ns          144 RowInvRate=4.75789us
BM_MatchStringsNonConstant/10000/40/20/3     48510030 ns     48508821 ns           15 RowInvRate=4.85088us
BM_MatchStringsNonConstant/100000/40/20/3   483826495 ns    483816465 ns            2 RowInvRate=4.83816us
BM_MatchStringsNonConstant/1000/60/20/3       5006836 ns      5006823 ns          135 RowInvRate=5.00682us
BM_MatchStringsNonConstant/10000/60/20/3     50774151 ns     50774262 ns           10 RowInvRate=5.07743us
BM_MatchStringsNonConstant/100000/60/20/3   507166767 ns    507152446 ns            2 RowInvRate=5.07152us
BM_MatchStringsNonConstant/1000/20/100/3      8821687 ns      8821559 ns           90 RowInvRate=8.82156us
BM_MatchStringsNonConstant/10000/20/100/3    77710276 ns     77709586 ns            9 RowInvRate=7.77096us
BM_MatchStringsNonConstant/100000/20/100/3  802545857 ns    802512004 ns            1 RowInvRate=8.02512us
BM_MatchStringsNonConstant/1000/40/100/3     11396580 ns     11395945 ns           61 RowInvRate=11.3959us
BM_MatchStringsNonConstant/10000/40/100/3   110271380 ns    110270647 ns            6 RowInvRate=11.0271us
BM_MatchStringsNonConstant/100000/40/100/3 1172333502 ns   1172318888 ns            1 RowInvRate=11.7232us
BM_MatchStringsNonConstant/1000/60/100/3     13890633 ns     13889238 ns           51 RowInvRate=13.8892us
BM_MatchStringsNonConstant/10000/60/100/3   146786265 ns    146775928 ns            5 RowInvRate=14.6776us
BM_MatchStringsNonConstant/100000/60/100/3 1463973006 ns   1463896432 ns            1 RowInvRate=14.639us
BM_MatchStringsConstant/1000/20/20/3           100017 ns       100039 ns         6487 RowInvRate=100.039ns
BM_MatchStringsConstant/10000/20/20/3          413616 ns       413618 ns         1676 RowInvRate=41.3618ns
BM_MatchStringsConstant/100000/20/20/3        3420512 ns      3420440 ns          203 RowInvRate=34.2044ns
BM_MatchStringsConstant/1000/40/20/3           200342 ns       200368 ns         3667 RowInvRate=200.368ns
BM_MatchStringsConstant/10000/40/20/3          504439 ns       504419 ns         1382 RowInvRate=50.4419ns
BM_MatchStringsConstant/100000/40/20/3        3526475 ns      3526484 ns          201 RowInvRate=35.2648ns
BM_MatchStringsConstant/1000/60/20/3           299573 ns       299609 ns         2455 RowInvRate=299.609ns
BM_MatchStringsConstant/10000/60/20/3          595700 ns       595708 ns         1178 RowInvRate=59.5708ns
BM_MatchStringsConstant/100000/60/20/3        3746117 ns      3746105 ns          191 RowInvRate=37.461ns
BM_MatchStringsConstant/1000/20/100/3          139044 ns       139066 ns         5063 RowInvRate=139.066ns
BM_MatchStringsConstant/10000/20/100/3         434958 ns       434953 ns         1632 RowInvRate=43.4953ns
BM_MatchStringsConstant/100000/20/100/3       3400974 ns      3401027 ns          200 RowInvRate=34.0103ns
BM_MatchStringsConstant/1000/40/100/3          345134 ns       345167 ns         1947 RowInvRate=345.167ns
BM_MatchStringsConstant/10000/40/100/3         680577 ns       680555 ns         1055 RowInvRate=68.0555ns
BM_MatchStringsConstant/100000/40/100/3       3744114 ns      3744050 ns          181 RowInvRate=37.4405ns
BM_MatchStringsConstant/1000/60/100/3          625749 ns       625776 ns         1067 RowInvRate=625.776ns
BM_MatchStringsConstant/10000/60/100/3         964268 ns       964239 ns          725 RowInvRate=96.4239ns
BM_MatchStringsConstant/100000/60/100/3       4073624 ns      4073420 ns          174 RowInvRate=40.7342ns
*/

enum MatchType {
    CONSTANT,
    NON_CONSTANT,
};

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

static void do_bench(benchmark::State& state, MatchType match_type) {
    int num_rows = state.range(0);
    int num_strings = state.range(1);
    int match_size = state.range(2);
    int top_k = state.range(3);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_strings - 1);

    std::vector<std::string> strings;
    strings.reserve(num_strings);
    for (int i = 0; i < num_strings; i++) {
        strings.push_back(generate_random_string(8, 12));
    }

    auto gen_rand_element = [&]() { return Slice(strings[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto top_k_column = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
        auto separator_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_rand_element());
            top_k_column->append_datum(top_k);
            separator_column->append_datum(",");
        }
        auto match_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        switch (match_type) {
        case CONSTANT:
            match_column->append_datum(gen_rand_array(match_size));
            match_column = ConstColumn::create(match_column, num_rows);
            top_k_column = ConstColumn::create(top_k_column, num_rows);
            separator_column = ConstColumn::create(separator_column, num_rows);
            ctx->set_constant_columns({nullptr, match_column, top_k_column, separator_column});
            break;
        case NON_CONSTANT:
            for (int i = 0; i < num_rows; i++) {
                match_column->append_datum(gen_rand_array(match_size));
            }
            ctx->set_constant_columns({nullptr, nullptr, nullptr, nullptr});
            break;
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisStringFunctions::match_strings_prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisStringFunctions::match_strings_prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisStringFunctions::match_strings(ctx.get(), {input_column, match_column, top_k_column, separator_column}).ok());
        ASSERT_TRUE(CelonisStringFunctions::match_strings_close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisStringFunctions::match_strings_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_MatchStringsNonConstant(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

static void BM_MatchStringsConstant(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

// Args: Number of rows / Number of possible strings / Size of match string / Value of top_k
BENCHMARK(BM_MatchStringsNonConstant)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {20, 100}, {3}});
BENCHMARK(BM_MatchStringsConstant)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {20, 100}, {3}});

} // namespace starrocks

BENCHMARK_MAIN();
