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
2025-06-09T09:59:53+00:00
Running ./be/build_Release/src/bench/celonis/output/match_strings_bench
Run on (32 X 3218.15 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.29, 5.01, 3.44
// Args: Number of rows / Number of possible strings / Size of match string / Value of top_k
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MatchStringsNonConstant/1000/20/20/3       4157064 ns      4156986 ns          166 RowInvRate=4.15699us
BM_MatchStringsNonConstant/10000/20/20/3     36683555 ns     36681166 ns           16 RowInvRate=3.66812us
BM_MatchStringsNonConstant/100000/20/20/3   380061892 ns    380052925 ns            2 RowInvRate=3.80053us
BM_MatchStringsNonConstant/1000/40/20/3       4805964 ns      4805793 ns          148 RowInvRate=4.80579us
BM_MatchStringsNonConstant/10000/40/20/3     47128090 ns     47127025 ns           15 RowInvRate=4.7127us
BM_MatchStringsNonConstant/100000/40/20/3   459665372 ns    459654309 ns            2 RowInvRate=4.59654us
BM_MatchStringsNonConstant/1000/60/20/3       5055963 ns      5055863 ns          100 RowInvRate=5.05586us
BM_MatchStringsNonConstant/10000/60/20/3     55117629 ns     55114890 ns           10 RowInvRate=5.51149us
BM_MatchStringsNonConstant/100000/60/20/3   521967685 ns    521963887 ns            2 RowInvRate=5.21964us
BM_MatchStringsNonConstant/1000/20/100/3      7996683 ns      7996730 ns           81 RowInvRate=7.99673us
BM_MatchStringsNonConstant/10000/20/100/3    82563426 ns     82563472 ns            8 RowInvRate=8.25635us
BM_MatchStringsNonConstant/100000/20/100/3  893985091 ns    893954705 ns            1 RowInvRate=8.93955us
BM_MatchStringsNonConstant/1000/40/100/3     11636225 ns     11636239 ns           59 RowInvRate=11.6362us
BM_MatchStringsNonConstant/10000/40/100/3   122248967 ns    122245112 ns            6 RowInvRate=12.2245us
BM_MatchStringsNonConstant/100000/40/100/3 1223174543 ns   1223106995 ns            1 RowInvRate=12.2311us
BM_MatchStringsNonConstant/1000/60/100/3     14836044 ns     14835339 ns           49 RowInvRate=14.8353us
BM_MatchStringsNonConstant/10000/60/100/3   140715911 ns    140708535 ns            5 RowInvRate=14.0709us
BM_MatchStringsNonConstant/100000/60/100/3 1484577582 ns   1484557852 ns            1 RowInvRate=14.8456us
BM_MatchStringsConstant/1000/20/20/3           104263 ns       104288 ns         6952 RowInvRate=104.288ns
BM_MatchStringsConstant/10000/20/20/3          405525 ns       405543 ns         1718 RowInvRate=40.5543ns
BM_MatchStringsConstant/100000/20/20/3        3415871 ns      3415863 ns          205 RowInvRate=34.1586ns
BM_MatchStringsConstant/1000/40/20/3           193350 ns       193380 ns         3482 RowInvRate=193.38ns
BM_MatchStringsConstant/10000/40/20/3          501348 ns       501314 ns         1370 RowInvRate=50.1314ns
BM_MatchStringsConstant/100000/40/20/3        3516970 ns      3516928 ns          202 RowInvRate=35.1693ns
BM_MatchStringsConstant/1000/60/20/3           295264 ns       295281 ns         2408 RowInvRate=295.281ns
BM_MatchStringsConstant/10000/60/20/3          592425 ns       592435 ns         1201 RowInvRate=59.2435ns
BM_MatchStringsConstant/100000/60/20/3        3604400 ns      3604375 ns          194 RowInvRate=36.0438ns
BM_MatchStringsConstant/1000/20/100/3          124248 ns       124272 ns         4823 RowInvRate=124.272ns
BM_MatchStringsConstant/10000/20/100/3         448232 ns       448238 ns         1557 RowInvRate=44.8238ns
BM_MatchStringsConstant/100000/20/100/3       3467389 ns      3467347 ns          202 RowInvRate=34.6735ns
BM_MatchStringsConstant/1000/40/100/3          348479 ns       348505 ns         1878 RowInvRate=348.505ns
BM_MatchStringsConstant/10000/40/100/3         651551 ns       651547 ns         1018 RowInvRate=65.1547ns
BM_MatchStringsConstant/100000/40/100/3       3679085 ns      3679044 ns          193 RowInvRate=36.7904ns
BM_MatchStringsConstant/1000/60/100/3          626326 ns       626373 ns         1139 RowInvRate=626.373ns
BM_MatchStringsConstant/10000/60/100/3         989287 ns       989280 ns          712 RowInvRate=98.928ns
BM_MatchStringsConstant/100000/60/100/3       4191137 ns      4191152 ns          175 RowInvRate=41.9115ns
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
    int64_t top_k = static_cast<int64_t>(state.range(3));

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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto top_k_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
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
        ASSERT_TRUE(CelonisStringFunctions::match_strings(ctx.get(),
                                                          {input_column, match_column, top_k_column, separator_column})
                            .ok());
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
