#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/like.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-09-05T23:27:59+00:00
Running ./be/build_Release/src/bench/celonis/output/like_bench
Run on (32 X 3103.74 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.83, 14.94, 15.58
// Args: Number of rows / Max string length / Pattern length
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_LikeConstantWildcard/1000/20/5           53760 ns        53747 ns        13066 RowInvRate=53.7469ns
BM_LikeConstantWildcard/10000/20/5         530958 ns       530893 ns         1317 RowInvRate=53.0893ns
BM_LikeConstantWildcard/100000/20/5       5260109 ns      5259648 ns          132 RowInvRate=52.5965ns
BM_LikeConstantWildcard/1000/60/5          107997 ns       107963 ns         6497 RowInvRate=107.963ns
BM_LikeConstantWildcard/10000/60/5        1078833 ns      1078722 ns          651 RowInvRate=107.872ns
BM_LikeConstantWildcard/100000/60/5      10700598 ns     10700151 ns           66 RowInvRate=107.002ns
BM_LikeConstantWildcard/1000/20/15          52982 ns        52968 ns        13237 RowInvRate=52.9681ns
BM_LikeConstantWildcard/10000/20/15        516347 ns       516302 ns         1351 RowInvRate=51.6302ns
BM_LikeConstantWildcard/100000/20/15      5099981 ns      5099730 ns          138 RowInvRate=50.9973ns
BM_LikeConstantWildcard/1000/60/15         108475 ns       108446 ns         6438 RowInvRate=108.446ns
BM_LikeConstantWildcard/10000/60/15       1071158 ns      1071066 ns          655 RowInvRate=107.107ns
BM_LikeConstantWildcard/100000/60/15     10662595 ns     10662136 ns           66 RowInvRate=106.621ns
BM_LikeConstantNoWildcard/1000/20/5         57351 ns        57334 ns        12206 RowInvRate=57.3335ns
BM_LikeConstantNoWildcard/10000/20/5       555158 ns       555118 ns         1254 RowInvRate=55.5118ns
BM_LikeConstantNoWildcard/100000/20/5     5540197 ns      5540090 ns          127 RowInvRate=55.4009ns
BM_LikeConstantNoWildcard/1000/60/5        113150 ns       113119 ns         6218 RowInvRate=113.119ns
BM_LikeConstantNoWildcard/10000/60/5      1121927 ns      1121778 ns          629 RowInvRate=112.178ns
BM_LikeConstantNoWildcard/100000/60/5    11172615 ns     11172063 ns           62 RowInvRate=111.721ns
BM_LikeConstantNoWildcard/1000/20/15        53322 ns        53307 ns        13058 RowInvRate=53.3066ns
BM_LikeConstantNoWildcard/10000/20/15      517051 ns       516967 ns         1353 RowInvRate=51.6967ns
BM_LikeConstantNoWildcard/100000/20/15    5152606 ns      5152182 ns          135 RowInvRate=51.5218ns
BM_LikeConstantNoWildcard/1000/60/15       111583 ns       111552 ns         6245 RowInvRate=111.552ns
BM_LikeConstantNoWildcard/10000/60/15     1104886 ns      1104706 ns          637 RowInvRate=110.471ns
BM_LikeConstantNoWildcard/100000/60/15   11048637 ns     11047982 ns           64 RowInvRate=110.48ns
BM_LikeNonConstant/1000/20/5              3249147 ns      3248848 ns          215 RowInvRate=3.24885us
BM_LikeNonConstant/10000/20/5            32391339 ns     32390089 ns           22 RowInvRate=3.23901us
BM_LikeNonConstant/100000/20/5          324162255 ns    324143976 ns            2 RowInvRate=3.24144us
BM_LikeNonConstant/1000/60/5              3491265 ns      3491123 ns          202 RowInvRate=3.49112us
BM_LikeNonConstant/10000/60/5            34790313 ns     34788851 ns           20 RowInvRate=3.47889us
BM_LikeNonConstant/100000/60/5          346851870 ns    346817955 ns            2 RowInvRate=3.46818us
BM_LikeNonConstant/1000/20/15             6105290 ns      6104782 ns          114 RowInvRate=6.10478us
BM_LikeNonConstant/10000/20/15           61016334 ns     61012681 ns           12 RowInvRate=6.10127us
BM_LikeNonConstant/100000/20/15         610050715 ns    609984269 ns            1 RowInvRate=6.09984us
BM_LikeNonConstant/1000/60/15             6880326 ns      6880026 ns          101 RowInvRate=6.88003us
BM_LikeNonConstant/10000/60/15           68318756 ns     68314470 ns           10 RowInvRate=6.83145us
BM_LikeNonConstant/100000/60/15         683432966 ns    683369958 ns            1 RowInvRate=6.8337us
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
            return Slice(std::string(alphanum.c_str() + str_start, 1) + "." +
                         std::string(alphanum.c_str() + str_start + 2, str_len - 2));
        }
        return Slice(alphanum.c_str() + str_start, str_len);
    };

    int num_rows = state.range(0);
    int max_str_length = state.range(1);
    int pattern_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_rand_str(1, max_str_length, false));
        }
        ColumnPtr pattern_column;
        switch (pattern_type) {
        case CONSTANT_WILDCARD:
            pattern_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(
                    gen_rand_str(pattern_length, pattern_length, true), num_rows);
            break;
        case CONSTANT_NO_WILDCARD:
            pattern_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(
                    gen_rand_str(pattern_length, pattern_length, false), num_rows);
            break;
        case NON_CONSTANT:
            pattern_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
            for (int i = 0; i < num_rows; i++) {
                pattern_column->append_datum(gen_rand_str(pattern_length, pattern_length, uniform_int(rng) % 2));
            }
        }
        Columns columns;
        columns.push_back(input_column);
        columns.push_back(pattern_column);
        ctx->set_constant_columns(columns);

        state.ResumeTiming();
        ASSERT_OK(CelonisLike::like_prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(CelonisLike::like_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(CelonisLike::like(ctx.get(), columns).ok());
        ASSERT_OK(CelonisLike::like_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(CelonisLike::like_close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_LikeConstantWildcard(benchmark::State& state) {
    do_bench(state, CONSTANT_WILDCARD);
}

static void BM_LikeConstantNoWildcard(benchmark::State& state) {
    do_bench(state, CONSTANT_NO_WILDCARD);
}

static void BM_LikeNonConstant(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

BENCHMARK(BM_LikeConstantWildcard)->ArgsProduct({{1000, 10000, 100000}, {20, 60}, {5, 15}});
BENCHMARK(BM_LikeConstantNoWildcard)->ArgsProduct({{1000, 10000, 100000}, {20, 60}, {5, 15}});
BENCHMARK(BM_LikeNonConstant)->ArgsProduct({{1000, 10000, 100000}, {20, 60}, {5, 15}});

} // namespace starrocks

BENCHMARK_MAIN();