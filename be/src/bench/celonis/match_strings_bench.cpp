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
2024-09-26T16:24:47+00:00
Running ./be/build_Release/src/bench/celonis/output/match_strings_bench
Run on (32 X 2876.67 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.05, 0.65, 0.86
// Args: Number of rows / Number of possible strings / Size of match string / Value of top_k
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_MatchStringsNonConstant/1000/20/20/3       4175839 ns      4175546 ns          172 RowInvRate=4.17555us
BM_MatchStringsNonConstant/10000/20/20/3     44193428 ns     44190683 ns           17 RowInvRate=4.41907us
BM_MatchStringsNonConstant/100000/20/20/3   411945827 ns    411906716 ns            2 RowInvRate=4.11907us
BM_MatchStringsNonConstant/1000/40/20/3       4732275 ns      4731876 ns          143 RowInvRate=4.73188us
BM_MatchStringsNonConstant/10000/40/20/3     52504918 ns     52501247 ns           10 RowInvRate=5.25012us
BM_MatchStringsNonConstant/100000/40/20/3   529819698 ns    529776210 ns            2 RowInvRate=5.29776us
BM_MatchStringsNonConstant/1000/60/20/3       5365858 ns      5364983 ns          130 RowInvRate=5.36498us
BM_MatchStringsNonConstant/10000/60/20/3     52673197 ns     52646358 ns           13 RowInvRate=5.26464us
BM_MatchStringsNonConstant/100000/60/20/3   512695362 ns    512663737 ns            1 RowInvRate=5.12664us
BM_MatchStringsNonConstant/1000/20/100/3      9997537 ns      9992363 ns           74 RowInvRate=9.99236us
BM_MatchStringsNonConstant/10000/20/100/3   112187659 ns    112172669 ns            7 RowInvRate=11.2173us
BM_MatchStringsNonConstant/100000/20/100/3 1103976267 ns   1103867022 ns            1 RowInvRate=11.0387us
BM_MatchStringsNonConstant/1000/40/100/3     13661360 ns     13655006 ns           48 RowInvRate=13.655us
BM_MatchStringsNonConstant/10000/40/100/3   152148038 ns    152126095 ns            5 RowInvRate=15.2126us
BM_MatchStringsNonConstant/100000/40/100/3 1464362873 ns   1464188045 ns            1 RowInvRate=14.6419us
BM_MatchStringsNonConstant/1000/60/100/3     16687628 ns     16680334 ns           40 RowInvRate=16.6803us
BM_MatchStringsNonConstant/10000/60/100/3   182615112 ns    182584308 ns            4 RowInvRate=18.2584us
BM_MatchStringsNonConstant/100000/60/100/3 1795416274 ns   1795218873 ns            1 RowInvRate=17.9522us
BM_MatchStringsConstant/1000/20/20/3          2774392 ns      2774225 ns          210 RowInvRate=2.77423us
BM_MatchStringsConstant/10000/20/20/3        30695186 ns     30690903 ns           25 RowInvRate=3.06909us
BM_MatchStringsConstant/100000/20/20/3      263345315 ns    263329524 ns            2 RowInvRate=2.6333us
BM_MatchStringsConstant/1000/40/20/3          3457420 ns      3456959 ns          198 RowInvRate=3.45696us
BM_MatchStringsConstant/10000/40/20/3        34045369 ns     34028996 ns           20 RowInvRate=3.4029us
BM_MatchStringsConstant/100000/40/20/3      385076932 ns    384946191 ns            2 RowInvRate=3.84946us
BM_MatchStringsConstant/1000/60/20/3          4070630 ns      4070441 ns          185 RowInvRate=4.07044us
BM_MatchStringsConstant/10000/60/20/3        38671301 ns     38668644 ns           17 RowInvRate=3.86686us
BM_MatchStringsConstant/100000/60/20/3      390859561 ns    390807240 ns            2 RowInvRate=3.90807us
BM_MatchStringsConstant/1000/20/100/3         3914847 ns      3914611 ns          163 RowInvRate=3.91461us
BM_MatchStringsConstant/10000/20/100/3       42305379 ns     42298491 ns           16 RowInvRate=4.22985us
BM_MatchStringsConstant/100000/20/100/3     436665729 ns    436627519 ns            2 RowInvRate=4.36628us
BM_MatchStringsConstant/1000/40/100/3         8108558 ns      8107876 ns           81 RowInvRate=8.10788us
BM_MatchStringsConstant/10000/40/100/3       84170709 ns     84159631 ns            8 RowInvRate=8.41596us
BM_MatchStringsConstant/100000/40/100/3     880085632 ns    879954234 ns            1 RowInvRate=8.79954us
BM_MatchStringsConstant/1000/60/100/3        10828970 ns     10827920 ns           65 RowInvRate=10.8279us
BM_MatchStringsConstant/10000/60/100/3      106959002 ns    106947559 ns            6 RowInvRate=10.6948us
BM_MatchStringsConstant/100000/60/100/3    1060738646 ns   1060632189 ns            1 RowInvRate=10.6063us
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
