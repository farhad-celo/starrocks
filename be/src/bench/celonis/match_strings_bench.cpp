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
2024-09-25T19:00:17+00:00
Running ./be/build_Release/src/bench/celonis/output/match_strings_bench
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.87, 2.22, 2.62
// Args: Number of rows / Number of possible strings / Size of match string / Value of top_k
----------------------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------
BM_MatchStringsNonConstant/1000/20/20/3      4501265 ns      4500997 ns          159 RowInvRate=4.501us
BM_MatchStringsNonConstant/10000/20/20/3    44493460 ns     44488369 ns           16 RowInvRate=4.44884us
BM_MatchStringsNonConstant/100000/20/20/3  438013944 ns    437935355 ns            2 RowInvRate=4.37935us
BM_MatchStringsNonConstant/1000/40/20/3      4966136 ns      4964909 ns          148 RowInvRate=4.96491us
BM_MatchStringsNonConstant/10000/40/20/3    50679347 ns     50657244 ns           14 RowInvRate=5.06572us
BM_MatchStringsNonConstant/100000/40/20/3  504908800 ns    504853481 ns            1 RowInvRate=5.04853us
BM_MatchStringsNonConstant/1000/60/20/3      5201940 ns      5200230 ns          130 RowInvRate=5.20023us
BM_MatchStringsNonConstant/10000/60/20/3    55031303 ns     55023915 ns           13 RowInvRate=5.50239us
BM_MatchStringsNonConstant/100000/60/20/3  520412600 ns    520293803 ns            1 RowInvRate=5.20294us
BM_MatchStringsConstant/1000/20/20/3         4100278 ns      4099878 ns          171 RowInvRate=4.09988us
BM_MatchStringsConstant/10000/20/20/3       41395806 ns     41389239 ns           18 RowInvRate=4.13892us
BM_MatchStringsConstant/100000/20/20/3     443574727 ns    443510015 ns            2 RowInvRate=4.4351us
BM_MatchStringsConstant/1000/40/20/3         4738929 ns      4738035 ns          146 RowInvRate=4.73803us
BM_MatchStringsConstant/10000/40/20/3       47934782 ns     47915145 ns           13 RowInvRate=4.79151us
BM_MatchStringsConstant/100000/40/20/3     487717489 ns    487655717 ns            2 RowInvRate=4.87656us
BM_MatchStringsConstant/1000/60/20/3         5166935 ns      5165286 ns          100 RowInvRate=5.16529us
BM_MatchStringsConstant/10000/60/20/3       50169740 ns     50155323 ns           14 RowInvRate=5.01553us
BM_MatchStringsConstant/100000/60/20/3     600069695 ns    599995673 ns            1 RowInvRate=5.99996us
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
        // ASSERT_TRUE(CelonisStringFunctions::match_strings_prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        // ASSERT_TRUE(CelonisStringFunctions::match_strings_prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisStringFunctions::match_strings(ctx.get(), {input_column, match_column, top_k_column, separator_column}).ok());
        // ASSERT_TRUE(CelonisStringFunctions::match_strings_close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        // ASSERT_TRUE(CelonisStringFunctions::match_strings_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
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
BENCHMARK(BM_MatchStringsNonConstant)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {20}, {3}});
BENCHMARK(BM_MatchStringsConstant)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {20}, {3}});

} // namespace starrocks

BENCHMARK_MAIN();
