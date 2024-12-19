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
Run on (32 X 2445.43 MHz CPU s)                                                                                                                                      [0/1931]
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.96, 4.48, 3.23
// Args: Number of rows / Number of possible strings / Size of match string / Value of top_k
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MatchStringsNonConstant/1000/20/20/3       4343124 ns      4343051 ns          164 RowInvRate=4.34305us
BM_MatchStringsNonConstant/10000/20/20/3     40510585 ns     40510596 ns           17 RowInvRate=4.05106us
BM_MatchStringsNonConstant/100000/20/20/3   414069364 ns    414054049 ns            2 RowInvRate=4.14054us
BM_MatchStringsNonConstant/1000/40/20/3       4741322 ns      4740921 ns          148 RowInvRate=4.74092us
BM_MatchStringsNonConstant/10000/40/20/3     48439059 ns     48435002 ns           15 RowInvRate=4.8435us
BM_MatchStringsNonConstant/100000/40/20/3   478147168 ns    478142933 ns            2 RowInvRate=4.78143us
BM_MatchStringsNonConstant/1000/60/20/3       5089573 ns      5089140 ns          137 RowInvRate=5.08914us
BM_MatchStringsNonConstant/10000/60/20/3     51607490 ns     51607538 ns           10 RowInvRate=5.16075us
BM_MatchStringsNonConstant/100000/60/20/3   516708618 ns    516327342 ns            1 RowInvRate=5.16327us
BM_MatchStringsNonConstant/1000/20/100/3      8081712 ns      8081017 ns           88 RowInvRate=8.08102us
BM_MatchStringsNonConstant/10000/20/100/3    81181311 ns     81180253 ns            9 RowInvRate=8.11803us
BM_MatchStringsNonConstant/100000/20/100/3  807589129 ns    807581005 ns            1 RowInvRate=8.07581us
BM_MatchStringsNonConstant/1000/40/100/3     11892999 ns     11890980 ns           63 RowInvRate=11.891us
BM_MatchStringsNonConstant/10000/40/100/3   118354539 ns    118353266 ns            6 RowInvRate=11.8353us
BM_MatchStringsNonConstant/100000/40/100/3 1194737453 ns   1194715152 ns            1 RowInvRate=11.9472us
BM_MatchStringsNonConstant/1000/60/100/3     14122787 ns     14122688 ns           51 RowInvRate=14.1227us
BM_MatchStringsNonConstant/10000/60/100/3   148076626 ns    148071502 ns            5 RowInvRate=14.8072us
BM_MatchStringsNonConstant/100000/60/100/3 1429979496 ns   1429971275 ns            1 RowInvRate=14.2997us
BM_MatchStringsConstant/1000/20/20/3           133631 ns       133621 ns         5049 RowInvRate=133.621ns
BM_MatchStringsConstant/10000/20/20/3          733747 ns       733678 ns          958 RowInvRate=73.3678ns
BM_MatchStringsConstant/100000/20/20/3        6545758 ns      6545628 ns          105 RowInvRate=65.4563ns
BM_MatchStringsConstant/1000/40/20/3           229707 ns       229721 ns         2951 RowInvRate=229.721ns
BM_MatchStringsConstant/10000/40/20/3          852305 ns       852209 ns          830 RowInvRate=85.2209ns
BM_MatchStringsConstant/100000/40/20/3        6844287 ns      6843980 ns          100 RowInvRate=68.4398ns
BM_MatchStringsConstant/1000/60/20/3           320749 ns       320766 ns         2113 RowInvRate=320.766ns
BM_MatchStringsConstant/10000/60/20/3          934822 ns       934828 ns          734 RowInvRate=93.4828ns
BM_MatchStringsConstant/100000/60/20/3        7152474 ns      7152305 ns           96 RowInvRate=71.5231ns
BM_MatchStringsConstant/1000/20/100/3          162204 ns       162216 ns         3989 RowInvRate=162.216ns
BM_MatchStringsConstant/10000/20/100/3         771181 ns       771109 ns         1003 RowInvRate=77.1109ns
BM_MatchStringsConstant/100000/20/100/3       6808352 ns      6808140 ns          108 RowInvRate=68.0814ns
BM_MatchStringsConstant/1000/40/100/3          405877 ns       405902 ns         1703 RowInvRate=405.902ns
BM_MatchStringsConstant/10000/40/100/3         973511 ns       973492 ns          731 RowInvRate=97.3492ns
BM_MatchStringsConstant/100000/40/100/3       6943463 ns      6943312 ns           99 RowInvRate=69.4331ns
BM_MatchStringsConstant/1000/60/100/3          694212 ns       694215 ns          992 RowInvRate=694.215ns
BM_MatchStringsConstant/10000/60/100/3        1327711 ns      1327673 ns          550 RowInvRate=132.767ns
BM_MatchStringsConstant/100000/60/100/3       7366925 ns      7366691 ns           94 RowInvRate=73.6669ns
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
            ctx->set_constant_columns({nullptr, match_column, nullptr, nullptr});
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
