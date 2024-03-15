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
Note that the performance is highly affected by how wildcards are placed in the pattern.
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.29, 1.29, 20.03
Args: Number of rows / maximum input string length / pattern string length / number of patterns
-------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations
-------------------------------------------------------------------------------------
BM_InLikeConstantWildcard/1000/20/5/1           98838 ns        98828 ns         7100
BM_InLikeConstantWildcard/10000/20/5/1         970702 ns       970649 ns          720
BM_InLikeConstantWildcard/100000/20/5/1       9644094 ns      9643218 ns           72
BM_InLikeConstantWildcard/1000/60/5/1          202401 ns       202378 ns         3447
BM_InLikeConstantWildcard/10000/60/5/1        1997487 ns      1997335 ns          351
BM_InLikeConstantWildcard/100000/60/5/1      19959426 ns     19957845 ns           35
BM_InLikeConstantWildcard/1000/20/15/1          98152 ns        98141 ns         7135
BM_InLikeConstantWildcard/10000/20/15/1        972548 ns       972514 ns          718
BM_InLikeConstantWildcard/100000/20/15/1      9640126 ns      9639451 ns           73
BM_InLikeConstantWildcard/1000/60/15/1         203312 ns       203301 ns         3474
BM_InLikeConstantWildcard/10000/60/15/1       2006945 ns      2006744 ns          351
BM_InLikeConstantWildcard/100000/60/15/1     20023272 ns     20022093 ns           35
BM_InLikeConstantWildcard/1000/20/5/3          188449 ns       188426 ns         3728
BM_InLikeConstantWildcard/10000/20/5/3        1879731 ns      1879648 ns          372
BM_InLikeConstantWildcard/100000/20/5/3      18739574 ns     18737594 ns           38
BM_InLikeConstantWildcard/1000/60/5/3          426869 ns       426819 ns         1640
BM_InLikeConstantWildcard/10000/60/5/3        4215056 ns      4214201 ns          166
BM_InLikeConstantWildcard/100000/60/5/3      42035737 ns     42031112 ns           17
BM_InLikeConstantWildcard/1000/20/15/3         189685 ns       189686 ns         3708
BM_InLikeConstantWildcard/10000/20/15/3       1855448 ns      1855236 ns          372
BM_InLikeConstantWildcard/100000/20/15/3     18590638 ns     18588756 ns           38
BM_InLikeConstantWildcard/1000/60/15/3         424426 ns       424381 ns         1648
BM_InLikeConstantWildcard/10000/60/15/3       4221851 ns      4221412 ns          166
BM_InLikeConstantWildcard/100000/60/15/3     42050310 ns     42047400 ns           17
BM_InLikeConstantNoWildcard/1000/20/5/1        100450 ns       100437 ns         6974
BM_InLikeConstantNoWildcard/10000/20/5/1       983994 ns       983905 ns          712
BM_InLikeConstantNoWildcard/100000/20/5/1     9760020 ns      9758696 ns           71
BM_InLikeConstantNoWildcard/1000/60/5/1        192545 ns       192524 ns         3624
BM_InLikeConstantNoWildcard/10000/60/5/1      1898969 ns      1898773 ns          368
BM_InLikeConstantNoWildcard/100000/60/5/1    18953532 ns     18951805 ns           37
BM_InLikeConstantNoWildcard/1000/20/15/1       103632 ns       103628 ns         6758
BM_InLikeConstantNoWildcard/10000/20/15/1     1015831 ns      1015760 ns          686
BM_InLikeConstantNoWildcard/100000/20/15/1   10171095 ns     10170143 ns           69
BM_InLikeConstantNoWildcard/1000/60/15/1       188696 ns       188677 ns         3701
BM_InLikeConstantNoWildcard/10000/60/15/1     1873032 ns      1872866 ns          376
BM_InLikeConstantNoWildcard/100000/60/15/1   18596174 ns     18594437 ns           38
BM_InLikeConstantNoWildcard/1000/20/5/3        161163 ns       161158 ns         4363
BM_InLikeConstantNoWildcard/10000/20/5/3      1580504 ns      1580333 ns          441
BM_InLikeConstantNoWildcard/100000/20/5/3    15735106 ns     15733753 ns           45
BM_InLikeConstantNoWildcard/1000/60/5/3        231284 ns       231269 ns         3027
BM_InLikeConstantNoWildcard/10000/60/5/3      2271435 ns      2271349 ns          303
BM_InLikeConstantNoWildcard/100000/60/5/3    22981104 ns     22979490 ns           30
BM_InLikeConstantNoWildcard/1000/20/15/3       197970 ns       197956 ns         3535
BM_InLikeConstantNoWildcard/10000/20/15/3     1954126 ns      1953915 ns          358
BM_InLikeConstantNoWildcard/100000/20/15/3   19444393 ns     19442645 ns           36
BM_InLikeConstantNoWildcard/1000/60/15/3       256243 ns       256221 ns         2733
BM_InLikeConstantNoWildcard/10000/60/15/3     2550547 ns      2550217 ns          276
BM_InLikeConstantNoWildcard/100000/60/15/3   24886402 ns     24884481 ns           29
BM_InLikeNonConstant/1000/20/5/1               157540 ns       157523 ns         4444
BM_InLikeNonConstant/10000/20/5/1             1559431 ns      1559298 ns          449
BM_InLikeNonConstant/100000/20/5/1           15608992 ns     15607576 ns           45
BM_InLikeNonConstant/1000/60/5/1               264129 ns       264120 ns         2650
BM_InLikeNonConstant/10000/60/5/1             2620801 ns      2620504 ns          267
BM_InLikeNonConstant/100000/60/5/1           26151616 ns     26149280 ns           27
BM_InLikeNonConstant/1000/20/15/1              227814 ns       227802 ns         3070
BM_InLikeNonConstant/10000/20/15/1            2264963 ns      2264759 ns          309
BM_InLikeNonConstant/100000/20/15/1          22720007 ns     22716925 ns           31
BM_InLikeNonConstant/1000/60/15/1              322742 ns       322720 ns         2168
BM_InLikeNonConstant/10000/60/15/1            3210153 ns      3209847 ns          218
BM_InLikeNonConstant/100000/60/15/1          32026991 ns     32024694 ns           22
BM_InLikeNonConstant/1000/20/5/3               382296 ns       382264 ns         1835
BM_InLikeNonConstant/10000/20/5/3             3806612 ns      3806082 ns          184
BM_InLikeNonConstant/100000/20/5/3           37954007 ns     37950458 ns           18
BM_InLikeNonConstant/1000/60/5/3               519450 ns       519395 ns         1366
BM_InLikeNonConstant/10000/60/5/3             5102853 ns      5102296 ns          138
BM_InLikeNonConstant/100000/60/5/3           51229396 ns     51224445 ns           13
BM_InLikeNonConstant/1000/20/15/3              625072 ns       625060 ns         1122
BM_InLikeNonConstant/10000/20/15/3            6217535 ns      6216998 ns          113
BM_InLikeNonConstant/100000/20/15/3          62326206 ns     62321442 ns           11
BM_InLikeNonConstant/1000/60/15/3              704269 ns       704207 ns          991
BM_InLikeNonConstant/10000/60/15/3            7017819 ns      7017285 ns          100
BM_InLikeNonConstant/100000/60/15/3          70763116 ns     70756963 ns           10
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

    for (auto _: state) {
        state.PauseTiming();
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_rand_str(1, max_str_length, false));
        }
        auto patterns_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), false);
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
BENCHMARK(BM_InLikeConstantWildcard)->ArgsProduct({{1000, 10000, 100000},
                                                   {20,   60},
                                                   {5,    15},
                                                   {1,    3}});
BENCHMARK(BM_InLikeConstantNoWildcard)->ArgsProduct({{1000, 10000, 100000},
                                                     {20,   60},
                                                     {5,    15},
                                                     {1,    3}});
BENCHMARK(BM_InLikeNonConstant)->ArgsProduct({{1000, 10000, 100000},
                                              {20,   60},
                                              {5,    15},
                                              {1,    3}});

} // namespace starrocks

BENCHMARK_MAIN();