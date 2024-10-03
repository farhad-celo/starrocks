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
2024-10-01T17:00:48+00:00
Running ./be/build_Release/src/bench/celonis/output/match_strings_bench
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.59, 3.02, 3.45
// Args: Number of rows / Number of possible strings / Size of match string / Value of top_k
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MatchStringsNonConstant/1000/20/20/3       3910418 ns      3910047 ns          190 RowInvRate=3.91005us
BM_MatchStringsNonConstant/10000/20/20/3     36747999 ns     36744463 ns           19 RowInvRate=3.67445us
BM_MatchStringsNonConstant/100000/20/20/3   382428397 ns    382396318 ns            2 RowInvRate=3.82396us
BM_MatchStringsNonConstant/1000/40/20/3       4443978 ns      4443027 ns          162 RowInvRate=4.44303us
BM_MatchStringsNonConstant/10000/40/20/3     44085068 ns     44082455 ns           16 RowInvRate=4.40825us
BM_MatchStringsNonConstant/100000/40/20/3   444623347 ns    444525434 ns            2 RowInvRate=4.44525us
BM_MatchStringsNonConstant/1000/60/20/3       4417671 ns      4416925 ns          153 RowInvRate=4.41692us
BM_MatchStringsNonConstant/10000/60/20/3     46689367 ns     46685581 ns           16 RowInvRate=4.66856us
BM_MatchStringsNonConstant/100000/60/20/3   485687728 ns    485640781 ns            2 RowInvRate=4.85641us
BM_MatchStringsNonConstant/1000/20/100/3      7702253 ns      7701822 ns           92 RowInvRate=7.70182us
BM_MatchStringsNonConstant/10000/20/100/3    73771281 ns     73765158 ns            9 RowInvRate=7.37652us
BM_MatchStringsNonConstant/100000/20/100/3  838247406 ns    838154603 ns            1 RowInvRate=8.38155us
BM_MatchStringsNonConstant/1000/40/100/3     10763699 ns     10762699 ns           66 RowInvRate=10.7627us
BM_MatchStringsNonConstant/10000/40/100/3   113814546 ns    113783921 ns            6 RowInvRate=11.3784us
BM_MatchStringsNonConstant/100000/40/100/3 1229956296 ns   1229694897 ns            1 RowInvRate=12.2969us
BM_MatchStringsNonConstant/1000/60/100/3     13093055 ns     13091084 ns           53 RowInvRate=13.0911us
BM_MatchStringsNonConstant/10000/60/100/3   134178551 ns    134165948 ns            5 RowInvRate=13.4166us
BM_MatchStringsNonConstant/100000/60/100/3 1435772549 ns   1435626564 ns            1 RowInvRate=14.3563us
BM_MatchStringsConstant/1000/20/20/3          2617683 ns      2616650 ns          238 RowInvRate=2.61665us
BM_MatchStringsConstant/10000/20/20/3        26938524 ns     26930692 ns           27 RowInvRate=2.69307us
BM_MatchStringsConstant/100000/20/20/3      284414585 ns    284397574 ns            2 RowInvRate=2.84398us
BM_MatchStringsConstant/1000/40/20/3          3273119 ns      3273065 ns          210 RowInvRate=3.27307us
BM_MatchStringsConstant/10000/40/20/3        35643987 ns     35641192 ns           20 RowInvRate=3.56412us
BM_MatchStringsConstant/100000/40/20/3      319904667 ns    319885696 ns            2 RowInvRate=3.19886us
BM_MatchStringsConstant/1000/60/20/3          3480782 ns      3480459 ns          199 RowInvRate=3.48046us
BM_MatchStringsConstant/10000/60/20/3        33330024 ns     33328099 ns           19 RowInvRate=3.33281us
BM_MatchStringsConstant/100000/60/20/3      351914629 ns    351879510 ns            2 RowInvRate=3.5188us
BM_MatchStringsConstant/1000/20/100/3         4047871 ns      4047412 ns          177 RowInvRate=4.04741us
BM_MatchStringsConstant/10000/20/100/3       39964576 ns     39959383 ns           17 RowInvRate=3.99594us
BM_MatchStringsConstant/100000/20/100/3     418867087 ns    418804426 ns            2 RowInvRate=4.18804us
BM_MatchStringsConstant/1000/40/100/3         6773855 ns      6773162 ns          103 RowInvRate=6.77316us
BM_MatchStringsConstant/10000/40/100/3       74858035 ns     74847214 ns           10 RowInvRate=7.48472us
BM_MatchStringsConstant/100000/40/100/3     684165213 ns    684065306 ns            1 RowInvRate=6.84065us
BM_MatchStringsConstant/1000/60/100/3         9043068 ns      9042523 ns           80 RowInvRate=9.04252us
BM_MatchStringsConstant/10000/60/100/3       91517197 ns     91506408 ns            8 RowInvRate=9.15064us
BM_MatchStringsConstant/100000/60/100/3     904520705 ns    904420683 ns            1 RowInvRate=9.04421us
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
