#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2024-12-29T22:37:53+00:00
Running ./be/build_Release/src/bench/celonis/output/in_like_bench
Run on (32 X 3201.71 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.18, 3.68, 2.56
// Args: Number of rows / Maximum input string length / Pattern string length / Number of patterns
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_InLikeConstantWildcard/1000/20/5/1           31347 ns        31296 ns        22060 RowInvRate=31.2956ns
BM_InLikeConstantWildcard/10000/20/5/1         301837 ns       301827 ns         2321 RowInvRate=30.1827ns
BM_InLikeConstantWildcard/100000/20/5/1       2988319 ns      2985067 ns          234 RowInvRate=29.8507ns
BM_InLikeConstantWildcard/1000/60/5/1           40288 ns        40272 ns        17343 RowInvRate=40.2721ns
BM_InLikeConstantWildcard/10000/60/5/1         388936 ns       388840 ns         1802 RowInvRate=38.884ns
BM_InLikeConstantWildcard/100000/60/5/1       3842472 ns      3842415 ns          182 RowInvRate=38.4241ns
BM_InLikeConstantWildcard/1000/20/15/1          31562 ns        31551 ns        22169 RowInvRate=31.5505ns
BM_InLikeConstantWildcard/10000/20/15/1        304372 ns       304346 ns         2302 RowInvRate=30.4346ns
BM_InLikeConstantWildcard/100000/20/15/1      3016206 ns      3016073 ns          231 RowInvRate=30.1607ns
BM_InLikeConstantWildcard/1000/60/15/1          40583 ns        40569 ns        17274 RowInvRate=40.5694ns
BM_InLikeConstantWildcard/10000/60/15/1        392790 ns       392759 ns         1780 RowInvRate=39.2759ns
BM_InLikeConstantWildcard/100000/60/15/1      3874952 ns      3874678 ns          181 RowInvRate=38.7468ns
BM_InLikeConstantWildcard/1000/20/5/3           31620 ns        31604 ns        22188 RowInvRate=31.6039ns
BM_InLikeConstantWildcard/10000/20/5/3         303963 ns       303974 ns         2291 RowInvRate=30.3974ns
BM_InLikeConstantWildcard/100000/20/5/3       3005294 ns      3005203 ns          231 RowInvRate=30.052ns
BM_InLikeConstantWildcard/1000/60/5/3           40937 ns        40918 ns        17119 RowInvRate=40.9184ns
BM_InLikeConstantWildcard/10000/60/5/3         392902 ns       392836 ns         1782 RowInvRate=39.2836ns
BM_InLikeConstantWildcard/100000/60/5/3       3866018 ns      3865852 ns          182 RowInvRate=38.6585ns
BM_InLikeConstantWildcard/1000/20/15/3          31895 ns        31882 ns        21988 RowInvRate=31.8823ns
BM_InLikeConstantWildcard/10000/20/15/3        304370 ns       304366 ns         2306 RowInvRate=30.4366ns
BM_InLikeConstantWildcard/100000/20/15/3      3010005 ns      3009904 ns          230 RowInvRate=30.099ns
BM_InLikeConstantWildcard/1000/60/15/3          40842 ns        40826 ns        17171 RowInvRate=40.8257ns
BM_InLikeConstantWildcard/10000/60/15/3        391674 ns       391620 ns         1783 RowInvRate=39.162ns
BM_InLikeConstantWildcard/100000/60/15/3      3877537 ns      3877452 ns          181 RowInvRate=38.7745ns
BM_InLikeConstantNoWildcard/1000/20/5/1         71157 ns        71148 ns         9848 RowInvRate=71.1476ns
BM_InLikeConstantNoWildcard/10000/20/5/1       692443 ns       692434 ns         1012 RowInvRate=69.2434ns
BM_InLikeConstantNoWildcard/100000/20/5/1     6842710 ns      6842736 ns          102 RowInvRate=68.4274ns
BM_InLikeConstantNoWildcard/1000/60/5/1        118233 ns       118219 ns         5926 RowInvRate=118.219ns
BM_InLikeConstantNoWildcard/10000/60/5/1      1157186 ns      1157088 ns          605 RowInvRate=115.709ns
BM_InLikeConstantNoWildcard/100000/60/5/1    11476971 ns     11476873 ns           61 RowInvRate=114.769ns
BM_InLikeConstantNoWildcard/1000/20/15/1        66909 ns        66900 ns        10407 RowInvRate=66.9003ns
BM_InLikeConstantNoWildcard/10000/20/15/1      655812 ns       655837 ns         1069 RowInvRate=65.5837ns
BM_InLikeConstantNoWildcard/100000/20/15/1    6498595 ns      6498594 ns          108 RowInvRate=64.9859ns
BM_InLikeConstantNoWildcard/1000/60/15/1       116429 ns       116414 ns         6028 RowInvRate=116.414ns
BM_InLikeConstantNoWildcard/10000/60/15/1     1141968 ns      1141846 ns          615 RowInvRate=114.185ns
BM_InLikeConstantNoWildcard/100000/60/15/1   11365963 ns     11365036 ns           62 RowInvRate=113.65ns
BM_InLikeConstantNoWildcard/1000/20/5/3         80858 ns        80847 ns         8621 RowInvRate=80.8468ns
BM_InLikeConstantNoWildcard/10000/20/5/3       774886 ns       774823 ns          905 RowInvRate=77.4823ns
BM_InLikeConstantNoWildcard/100000/20/5/3     7651468 ns      7650887 ns           92 RowInvRate=76.5089ns
BM_InLikeConstantNoWildcard/1000/60/5/3        124434 ns       124429 ns         5630 RowInvRate=124.429ns
BM_InLikeConstantNoWildcard/10000/60/5/3      1206962 ns      1206865 ns          578 RowInvRate=120.686ns
BM_InLikeConstantNoWildcard/100000/60/5/3    11996447 ns     11996087 ns           59 RowInvRate=119.961ns
BM_InLikeConstantNoWildcard/1000/20/15/3        74217 ns        74206 ns         9430 RowInvRate=74.206ns
BM_InLikeConstantNoWildcard/10000/20/15/3      721773 ns       721755 ns          968 RowInvRate=72.1755ns
BM_InLikeConstantNoWildcard/100000/20/15/3    7106935 ns      7106853 ns           99 RowInvRate=71.0685ns
BM_InLikeConstantNoWildcard/1000/60/15/3       122065 ns       122055 ns         5738 RowInvRate=122.055ns
BM_InLikeConstantNoWildcard/10000/60/15/3     1191961 ns      1191914 ns          589 RowInvRate=119.191ns
BM_InLikeConstantNoWildcard/100000/60/15/3   11837586 ns     11836886 ns           59 RowInvRate=118.369ns
BM_InLikeNonConstant/1000/20/5/1               152458 ns       152447 ns         4618 RowInvRate=152.447ns
BM_InLikeNonConstant/10000/20/5/1             1518557 ns      1518520 ns          460 RowInvRate=151.852ns
BM_InLikeNonConstant/100000/20/5/1           15205253 ns     15204858 ns           46 RowInvRate=152.049ns
BM_InLikeNonConstant/1000/60/5/1               178204 ns       178184 ns         3928 RowInvRate=178.184ns
BM_InLikeNonConstant/10000/60/5/1             1765868 ns      1765799 ns          396 RowInvRate=176.58ns
BM_InLikeNonConstant/100000/60/5/1           17595687 ns     17594785 ns           40 RowInvRate=175.948ns
BM_InLikeNonConstant/1000/20/15/1              177225 ns       177201 ns         3931 RowInvRate=177.201ns
BM_InLikeNonConstant/10000/20/15/1            1763678 ns      1763561 ns          398 RowInvRate=176.356ns
BM_InLikeNonConstant/100000/20/15/1          17469755 ns     17468819 ns           40 RowInvRate=174.688ns
BM_InLikeNonConstant/1000/60/15/1              206093 ns       206091 ns         3404 RowInvRate=206.091ns
BM_InLikeNonConstant/10000/60/15/1            2039256 ns      2039056 ns          345 RowInvRate=203.906ns
BM_InLikeNonConstant/100000/60/15/1          20294726 ns     20293768 ns           35 RowInvRate=202.938ns
BM_InLikeNonConstant/1000/20/5/3               360358 ns       360342 ns         1944 RowInvRate=360.342ns
BM_InLikeNonConstant/10000/20/5/3             3571632 ns      3571605 ns          196 RowInvRate=357.161ns
BM_InLikeNonConstant/100000/20/5/3           35627446 ns     35627401 ns           20 RowInvRate=356.274ns
BM_InLikeNonConstant/1000/60/5/3               352260 ns       352236 ns         1996 RowInvRate=352.236ns
BM_InLikeNonConstant/10000/60/5/3             3480948 ns      3480879 ns          201 RowInvRate=348.088ns
BM_InLikeNonConstant/100000/60/5/3           34647400 ns     34645758 ns           20 RowInvRate=346.458ns
BM_InLikeNonConstant/1000/20/15/3              470924 ns       470914 ns         1483 RowInvRate=470.914ns
BM_InLikeNonConstant/10000/20/15/3            4704155 ns      4704001 ns          149 RowInvRate=470.4ns
BM_InLikeNonConstant/100000/20/15/3          46953312 ns     46949945 ns           15 RowInvRate=469.499ns
BM_InLikeNonConstant/1000/60/15/3              450782 ns       450762 ns         1553 RowInvRate=450.762ns
BM_InLikeNonConstant/10000/60/15/3            4481334 ns      4481080 ns          157 RowInvRate=448.108ns
BM_InLikeNonConstant/100000/60/15/3          44524654 ns     44523404 ns           16 RowInvRate=445.234ns
*/

enum PatternType {
    CONSTANT_WILDCARD,
    CONSTANT_NO_WILDCARD,
    NON_CONSTANT,
};

static void do_bench(benchmark::State& state, PatternType pattern_type) {
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_int;
    uniform_int.param(UniformInt::param_type(1, 250));

    static std::string alphanum =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    auto gen_rand_str = [&](int min_length, int max_length, bool insert_wildcard) {
        int str_len =
                (min_length == max_length) ? min_length : min_length + uniform_int(rng) % (max_length - min_length);
        int str_start = uniform_int(rng) % (alphanum.size() - str_len);
        if (insert_wildcard) {
            return Slice(std::string(alphanum.c_str() + str_start, 1) + "_" +
                         std::string(alphanum.c_str() + str_start + 2, str_len - 2));
        }
        return Slice(alphanum.c_str() + str_start, str_len);
    };

    int num_rows = state.range(0);
    int max_str_length = state.range(1);
    int pattern_length = state.range(2);
    int num_patterns = state.range(3);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_rand_str(1, max_str_length, false));
        }
        auto patterns_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), false);
        switch (pattern_type) {
        case CONSTANT_WILDCARD: {
            DatumArray array = {};
            for (int i = 0; i < num_patterns; ++i) {
                array.push_back(gen_rand_str(pattern_length, pattern_length, true));
            }
            patterns_column->append_datum(array);
            break;
        }
        case CONSTANT_NO_WILDCARD: {
            DatumArray array = {};
            for (int i = 0; i < num_patterns; ++i) {
                array.push_back(gen_rand_str(pattern_length, pattern_length, false));
            }
            patterns_column->append_datum(array);
            break;
        }
        case NON_CONSTANT: {
            for (int i = 0; i < num_rows; i++) {
                DatumArray array = {};
                for (int j = 0; j < num_patterns; ++j) {
                    array.push_back(gen_rand_str(pattern_length, pattern_length, uniform_int(rng) % 2));
                }
                patterns_column->append_datum(array);
            }
        }
        }
        if (pattern_type == CONSTANT_NO_WILDCARD || pattern_type == CONSTANT_WILDCARD) {
            ctx->set_constant_columns({nullptr, patterns_column});
        } else {
            ctx->set_constant_columns({nullptr, nullptr});
        }
        Columns columns;
        columns.push_back(input_column);
        columns.push_back(patterns_column);

        state.ResumeTiming();
        ASSERT_OK(CelonisStringFunctions::in_like_prepare(ctx.get(),
                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(
                CelonisStringFunctions::in_like_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisStringFunctions::in_like(ctx.get(), columns).ok());
        ASSERT_OK(CelonisStringFunctions::in_like_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(
                CelonisStringFunctions::in_like_close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_InLikeConstantWildcard(benchmark::State& state) {
    do_bench(state, CONSTANT_WILDCARD);
}

static void BM_InLikeConstantNoWildcard(benchmark::State& state) {
    do_bench(state, CONSTANT_NO_WILDCARD);
}

static void BM_InLikeNonConstant(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

// Args: Number of rows / maximum input string length / pattern string length / number of patterns
BENCHMARK(BM_InLikeConstantWildcard)->ArgsProduct({{1000, 10000, 100000}, {20, 60}, {5, 15}, {1, 3}});
BENCHMARK(BM_InLikeConstantNoWildcard)->ArgsProduct({{1000, 10000, 100000}, {20, 60}, {5, 15}, {1, 3}});
BENCHMARK(BM_InLikeNonConstant)->ArgsProduct({{1000, 10000, 100000}, {20, 60}, {5, 15}, {1, 3}});

} // namespace starrocks

BENCHMARK_MAIN();