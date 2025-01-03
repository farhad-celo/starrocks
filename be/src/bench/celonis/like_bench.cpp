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
2024-12-29T22:33:13+00:00
Running ./be/build_Release/src/bench/celonis/output/like_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.22, 5.40, 2.62
// Args: Number of rows / Max string length / Pattern length
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_LikeConstantWildcard/1000/20/5           89292 ns        89306 ns         7842 RowInvRate=89.3061ns
BM_LikeConstantWildcard/10000/20/5         826266 ns       826224 ns          851 RowInvRate=82.6224ns
BM_LikeConstantWildcard/100000/20/5       8198292 ns      8198355 ns           86 RowInvRate=81.9835ns
BM_LikeConstantWildcard/1000/60/5           97526 ns        97523 ns         7141 RowInvRate=97.5228ns
BM_LikeConstantWildcard/10000/60/5         902296 ns       902239 ns          779 RowInvRate=90.2239ns
BM_LikeConstantWildcard/100000/60/5       8931913 ns      8931816 ns           78 RowInvRate=89.3182ns
BM_LikeConstantWildcard/1000/20/15          94931 ns        94943 ns         7377 RowInvRate=94.9433ns
BM_LikeConstantWildcard/10000/20/15        827592 ns       827564 ns          846 RowInvRate=82.7564ns
BM_LikeConstantWildcard/100000/20/15      8078443 ns      8078090 ns           86 RowInvRate=80.7809ns
BM_LikeConstantWildcard/1000/60/15         103426 ns       103424 ns         6818 RowInvRate=103.424ns
BM_LikeConstantWildcard/10000/60/15        907072 ns       907019 ns          776 RowInvRate=90.7019ns
BM_LikeConstantWildcard/100000/60/15      8943143 ns      8942794 ns           78 RowInvRate=89.4279ns
BM_LikeConstantNoWildcard/1000/20/5         97221 ns        97231 ns         7186 RowInvRate=97.2305ns
BM_LikeConstantNoWildcard/10000/20/5       889037 ns       888987 ns          792 RowInvRate=88.8987ns
BM_LikeConstantNoWildcard/100000/20/5     8702012 ns      8701939 ns           80 RowInvRate=87.0194ns
BM_LikeConstantNoWildcard/1000/60/5        106591 ns       106581 ns         6569 RowInvRate=106.581ns
BM_LikeConstantNoWildcard/10000/60/5       979461 ns       979423 ns          716 RowInvRate=97.9423ns
BM_LikeConstantNoWildcard/100000/60/5     9673845 ns      9673403 ns           73 RowInvRate=96.734ns
BM_LikeConstantNoWildcard/1000/20/15       107289 ns       107297 ns         6490 RowInvRate=107.297ns
BM_LikeConstantNoWildcard/10000/20/15      894322 ns       894325 ns          785 RowInvRate=89.4325ns
BM_LikeConstantNoWildcard/100000/20/15    8739627 ns      8739648 ns           82 RowInvRate=87.3965ns
BM_LikeConstantNoWildcard/1000/60/15       124727 ns       124730 ns         5666 RowInvRate=124.73ns
BM_LikeConstantNoWildcard/10000/60/15     1063898 ns      1063838 ns          658 RowInvRate=106.384ns
BM_LikeConstantNoWildcard/100000/60/15   10435716 ns     10435616 ns           68 RowInvRate=104.356ns
BM_LikeNonConstant/1000/20/5              3280466 ns      3280455 ns          212 RowInvRate=3.28045us
BM_LikeNonConstant/10000/20/5            33112174 ns     33111961 ns           21 RowInvRate=3.3112us
BM_LikeNonConstant/100000/20/5          330278579 ns    330263180 ns            2 RowInvRate=3.30263us
BM_LikeNonConstant/1000/60/5              3560658 ns      3560720 ns          196 RowInvRate=3.56072us
BM_LikeNonConstant/10000/60/5            35372120 ns     35371925 ns           20 RowInvRate=3.53719us
BM_LikeNonConstant/100000/60/5          353246361 ns    353243685 ns            2 RowInvRate=3.53244us
BM_LikeNonConstant/1000/20/15             6291374 ns      6291319 ns          112 RowInvRate=6.29132us
BM_LikeNonConstant/10000/20/15           62981635 ns     62980105 ns           11 RowInvRate=6.29801us
BM_LikeNonConstant/100000/20/15         630438598 ns    630353273 ns            1 RowInvRate=6.30353us
BM_LikeNonConstant/1000/60/15             7132028 ns      7132001 ns           99 RowInvRate=7.132us
BM_LikeNonConstant/10000/60/15           70863487 ns     70859759 ns           10 RowInvRate=7.08598us
BM_LikeNonConstant/100000/60/15         707132046 ns    707112826 ns            1 RowInvRate=7.07113us
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