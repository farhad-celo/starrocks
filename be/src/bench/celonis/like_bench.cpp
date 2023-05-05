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
BM_LikeConstantWildcard is highly affected by how wildcards are placed in the pattern.
---------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations
---------------------------------------------------------------------------------
BM_LikeConstantWildcard/1000/20/5           87274 ns        87327 ns         7864
BM_LikeConstantWildcard/10000/20/5         813526 ns       813450 ns          838
BM_LikeConstantWildcard/100000/20/5       7975144 ns      7973922 ns           88
BM_LikeConstantWildcard/1000/60/5          100472 ns       100505 ns         6888
BM_LikeConstantWildcard/10000/60/5         931581 ns       931595 ns          752
BM_LikeConstantWildcard/100000/60/5       9209893 ns      9209552 ns           76
BM_LikeConstantWildcard/1000/20/15          93729 ns        93796 ns         7448
BM_LikeConstantWildcard/10000/20/15        819257 ns       819286 ns          851
BM_LikeConstantWildcard/100000/20/15      7947618 ns      7947463 ns           88
BM_LikeConstantWildcard/1000/60/15         106751 ns       106787 ns         6564
BM_LikeConstantWildcard/10000/60/15        928695 ns       928614 ns          747
BM_LikeConstantWildcard/100000/60/15      9171098 ns      9170157 ns           75

BM_LikeConstantNoWildcard/1000/20/5         93085 ns        93116 ns         7505
BM_LikeConstantNoWildcard/10000/20/5       844603 ns       844490 ns          828
BM_LikeConstantNoWildcard/100000/20/5     8251681 ns      8251300 ns           85
BM_LikeConstantNoWildcard/1000/60/5        103700 ns       103720 ns         6750
BM_LikeConstantNoWildcard/10000/60/5       931253 ns       931239 ns          750
BM_LikeConstantNoWildcard/100000/60/5     9437870 ns      9437758 ns           75
BM_LikeConstantNoWildcard/1000/20/15       104329 ns       104383 ns         6684
BM_LikeConstantNoWildcard/10000/20/15      864318 ns       864351 ns          812
BM_LikeConstantNoWildcard/100000/20/15    8054359 ns      8053906 ns           83
BM_LikeConstantNoWildcard/1000/60/15       114732 ns       114765 ns         6071
BM_LikeConstantNoWildcard/10000/60/15      966531 ns       966539 ns          724
BM_LikeConstantNoWildcard/100000/60/15    9440288 ns      9440144 ns           75

BM_LikeNonConstant/1000/20/5              5409804 ns      5409689 ns          129
BM_LikeNonConstant/10000/20/5            53835214 ns     53833980 ns           13
BM_LikeNonConstant/100000/20/5          540993565 ns    540967451 ns            1
BM_LikeNonConstant/1000/60/5              7380512 ns      7380503 ns          123
BM_LikeNonConstant/10000/60/5            57079359 ns     57078472 ns            9
BM_LikeNonConstant/100000/60/5          568246628 ns    568179576 ns            1
BM_LikeNonConstant/1000/20/15            11028404 ns     11028096 ns           63
BM_LikeNonConstant/10000/20/15          109765682 ns    109764454 ns            6
BM_LikeNonConstant/100000/20/15        1097157560 ns   1097111887 ns            1
BM_LikeNonConstant/1000/60/15            11897690 ns     11897615 ns           59
BM_LikeNonConstant/10000/60/15          118624610 ns    118620731 ns            6
BM_LikeNonConstant/100000/60/15        1188610670 ns   1188567362 ns            1
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

    for (auto _ : state) {
        state.PauseTiming();
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