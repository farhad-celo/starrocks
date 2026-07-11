#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/like/in_like.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

// Benchmark results: https://gist.github.com/OliLay/9f6c48c68147d9fa4f629954f6d97298

struct InLikeFunctions {
    using PrepareFunc = std::function<Status(FunctionContext*, FunctionContext::FunctionStateScope)>;
    using CloseFunc = std::function<Status(FunctionContext*, FunctionContext::FunctionStateScope)>;
    using ExecFunc = std::function<StatusOr<ColumnPtr>(FunctionContext*, const Columns&)>;

    PrepareFunc prepare_fn;
    ExecFunc exec_fn;
    CloseFunc close_fn;
};

const InLikeFunctions in_like_v1 = {.prepare_fn = celonis::like::v1::CelonisInLike::in_like_prepare,
                                    .exec_fn = celonis::like::v1::CelonisInLike::in_like,
                                    .close_fn = celonis::like::v1::CelonisInLike::in_like_close};

const InLikeFunctions in_like_v2 = {.prepare_fn = celonis::like::v2::CelonisInLike::in_like_prepare,
                                    .exec_fn = celonis::like::v2::CelonisInLike::in_like,
                                    .close_fn = celonis::like::v2::CelonisInLike::in_like_close};

enum PatternType {
    CONSTANT_WILDCARD,
    CONSTANT_ENDS_WITH,
    CONSTANT_NO_WILDCARD,
    NON_CONSTANT,
};

enum InLikeVersion { V1 = 1, V2 = 2 };

static void do_bench(benchmark::State& state, PatternType pattern_type) {
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());

    static std::string alphanum =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    auto gen_rand_str = [&](int min_length, int max_length, bool insert_wildcard) {
        UniformInt uniform_int(1, 250);
        int str_len =
                (min_length == max_length) ? min_length : min_length + uniform_int(rng) % (max_length - min_length);
        int str_start = uniform_int(rng) % (alphanum.size() - str_len);
        if (insert_wildcard) {
            return Slice(std::string(alphanum.c_str() + str_start, 1) + "_" +
                         std::string(alphanum.c_str() + str_start + 2, str_len - 2));
        }
        return Slice(alphanum.c_str() + str_start, str_len);
    };

    InLikeVersion in_like_version = static_cast<InLikeVersion>(state.range(0));
    int num_rows = state.range(1);
    int max_str_length = state.range(2);
    int pattern_length = state.range(3);
    int num_patterns = state.range(4);

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                        TypeDescriptor::from_logical_type(TYPE_ARRAY)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    InLikeFunctions in_like_impl;
    switch (in_like_version) {
    case V1: {
        in_like_impl = in_like_v1;
        break;
    }
    case V2: {
        in_like_impl = in_like_v2;
        break;
    }
    default: {
        throw std::runtime_error("Invalid in_like version");
    }
    }

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        if (pattern_type == CONSTANT_ENDS_WITH) {
            UniformInt uniform_int(0, num_patterns * 2); // To also have not matching inputs.

            while (input_column->size() < num_rows) {
                auto str = std::string(gen_rand_str(1, max_str_length, false)) + std::to_string(uniform_int(rng));
                input_column->append_datum(Slice(str));
            }
        } else {
            for (int i = 0; i < num_rows; i++) {
                input_column->append_datum(gen_rand_str(1, max_str_length, false));
            }
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
        case CONSTANT_ENDS_WITH: {
            DatumArray array = {};
            for (int i = 0; i < num_patterns; ++i) {
                array.push_back(Slice("%" + std::to_string(i)));
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
                UniformInt uniform_int(1, 250);
                for (int j = 0; j < num_patterns; ++j) {
                    array.push_back(gen_rand_str(pattern_length, pattern_length, uniform_int(rng) % 2));
                }
                patterns_column->append_datum(array);
            }
        }
        }
        if (pattern_type == NON_CONSTANT) {
            ctx->set_constant_columns({nullptr, nullptr});
        } else {
            ctx->set_constant_columns({nullptr, patterns_column});
        }
        Columns columns;
        columns.push_back(input_column);
        columns.push_back(patterns_column);

        state.ResumeTiming();

        ASSERT_OK(in_like_impl.prepare_fn(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSERT_OK(in_like_impl.prepare_fn(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        EXPECT_TRUE(in_like_impl.exec_fn(ctx.get(), columns).ok());
        ASSERT_OK(in_like_impl.close_fn(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        ASSERT_OK(in_like_impl.close_fn(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_InLikeConstantWildcard(benchmark::State& state) {
    do_bench(state, CONSTANT_WILDCARD);
}

static void BM_InLikeEndsWith(benchmark::State& state) {
    do_bench(state, CONSTANT_ENDS_WITH);
}

static void BM_InLikeConstantNoWildcard(benchmark::State& state) {
    do_bench(state, CONSTANT_NO_WILDCARD);
}

static void BM_InLikeNonConstant(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

// Args: in_like version / Number of rows / maximum input string length / pattern string length / number of patterns
BENCHMARK(BM_InLikeConstantWildcard)
        ->ArgsProduct({{V1, V2}, {1000, 10'000, 100'000, 1'000'000}, {20, 60}, {5, 15}, {1, 3}});
BENCHMARK(BM_InLikeConstantNoWildcard)
        ->ArgsProduct({{V1, V2}, {1000, 10'000, 100'000, 1'000'000}, {20, 60}, {5, 15}, {1, 3}});
BENCHMARK(BM_InLikeNonConstant)->ArgsProduct({{V1, V2}, {1000, 10'000, 100'000, 1'000'000}, {20, 60}, {5, 15}, {1, 3}});
BENCHMARK(BM_InLikeEndsWith)
        ->ArgsProduct({{V1, V2}, {100'000, 1'000'000}, {20, 60}, {3, 10, 15}, {1, 3, 10, 20, 40, 80, 200, 1000}});

} // namespace starrocks

BENCHMARK_MAIN();