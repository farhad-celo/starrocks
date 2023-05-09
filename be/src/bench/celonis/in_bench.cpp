#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/in.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
Args: Number of rows / Number of possible values / Size of match list
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_InNonConstantVARCHAR/1000/20/5        236715 ns       236554 ns         3046 RowInvRate=236.554ns
BM_InNonConstantVARCHAR/10000/20/5      2326251 ns      2326178 ns          303 RowInvRate=232.618ns
BM_InNonConstantVARCHAR/100000/20/5    23320265 ns     23318973 ns           30 RowInvRate=233.19ns
BM_InNonConstantVARCHAR/1000/40/5        235520 ns       235436 ns         2964 RowInvRate=235.436ns
BM_InNonConstantVARCHAR/10000/40/5      2320640 ns      2320565 ns          301 RowInvRate=232.056ns
BM_InNonConstantVARCHAR/100000/40/5    23483022 ns     23481988 ns           30 RowInvRate=234.82ns
BM_InNonConstantVARCHAR/1000/60/5        234473 ns       234400 ns         2996 RowInvRate=234.4ns
BM_InNonConstantVARCHAR/10000/60/5      2334787 ns      2334563 ns          302 RowInvRate=233.456ns
BM_InNonConstantVARCHAR/100000/60/5    23322036 ns     23318611 ns           30 RowInvRate=233.186ns
BM_InNonConstantVARCHAR/1000/20/10       364457 ns       364379 ns         1931 RowInvRate=364.379ns
BM_InNonConstantVARCHAR/10000/20/10     3647502 ns      3647007 ns          192 RowInvRate=364.701ns
BM_InNonConstantVARCHAR/100000/20/10   36523167 ns     36518251 ns           19 RowInvRate=365.183ns
BM_InNonConstantVARCHAR/1000/40/10       366788 ns       366717 ns         1911 RowInvRate=366.717ns
BM_InNonConstantVARCHAR/10000/40/10     3642717 ns      3642433 ns          192 RowInvRate=364.243ns
BM_InNonConstantVARCHAR/100000/40/10   36638526 ns     36635546 ns           19 RowInvRate=366.355ns
BM_InNonConstantVARCHAR/1000/60/10       364774 ns       364683 ns         1919 RowInvRate=364.683ns
BM_InNonConstantVARCHAR/10000/60/10     3612456 ns      3612297 ns          194 RowInvRate=361.23ns
BM_InNonConstantVARCHAR/100000/60/10   35933528 ns     35930520 ns           19 RowInvRate=359.305ns
BM_InConstantVARCHAR/1000/20/5            19449 ns        19444 ns        36064 RowInvRate=19.4438ns
BM_InConstantVARCHAR/10000/20/5          185556 ns       185564 ns         3765 RowInvRate=18.5564ns
BM_InConstantVARCHAR/100000/20/5        1827856 ns      1827643 ns          382 RowInvRate=18.2764ns
BM_InConstantVARCHAR/1000/40/5            17706 ns        17705 ns        40648 RowInvRate=17.7045ns
BM_InConstantVARCHAR/10000/40/5          164599 ns       164606 ns         4224 RowInvRate=16.4606ns
BM_InConstantVARCHAR/100000/40/5        1749046 ns      1748930 ns          423 RowInvRate=17.4893ns
BM_InConstantVARCHAR/1000/60/5            16039 ns        16034 ns        43576 RowInvRate=16.0344ns
BM_InConstantVARCHAR/10000/60/5          153260 ns       153260 ns         4658 RowInvRate=15.326ns
BM_InConstantVARCHAR/100000/60/5        1485737 ns      1485674 ns          471 RowInvRate=14.8567ns
BM_InConstantVARCHAR/1000/20/10           22202 ns        22196 ns        31544 RowInvRate=22.196ns
BM_InConstantVARCHAR/10000/20/10         209789 ns       209774 ns         3334 RowInvRate=20.9774ns
BM_InConstantVARCHAR/100000/20/10       2056274 ns      2055890 ns          337 RowInvRate=20.5589ns
BM_InConstantVARCHAR/1000/40/10           19155 ns        19162 ns        36132 RowInvRate=19.1617ns
BM_InConstantVARCHAR/10000/40/10         178992 ns       179005 ns         3925 RowInvRate=17.9005ns
BM_InConstantVARCHAR/100000/40/10       1761785 ns      1761722 ns          397 RowInvRate=17.6172ns
BM_InConstantVARCHAR/1000/60/10           17684 ns        17677 ns        39527 RowInvRate=17.6766ns
BM_InConstantVARCHAR/10000/60/10         163728 ns       163722 ns         4287 RowInvRate=16.3722ns
BM_InConstantVARCHAR/100000/60/10       1613342 ns      1613203 ns          437 RowInvRate=16.132ns
 */

enum MatchType {
    CONSTANT,
    NON_CONSTANT,
};

static void do_bench(benchmark::State& state, MatchType match_type) {
    int num_rows = state.range(0);
    int num_values = state.range(1);
    int match_size = state.range(2);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_values - 1);

    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    auto gen_rand_element = [&]() { return Slice(values[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_rand_element());
        }
        auto match_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        switch (match_type) {
        case CONSTANT:
            match_column->append_datum(gen_rand_array(match_size));
            match_column = ConstColumn::create(match_column, num_rows);
            ctx->set_constant_columns({nullptr, match_column});
            break;
        case NON_CONSTANT:
            for (int i = 0; i < num_rows; i++) {
                match_column->append_datum(gen_rand_array(match_size));
            }
            ctx->set_constant_columns({nullptr, nullptr});
            break;
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisIn::celonis_in(ctx.get(), {input_column, match_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_InNonConstantVARCHAR(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

static void BM_InConstantVARCHAR(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

// Args: Number of rows / Number of possible values / Size of match list
BENCHMARK(BM_InNonConstantVARCHAR)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {5, 10}});
BENCHMARK(BM_InConstantVARCHAR)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {5, 10}});

} // namespace starrocks

BENCHMARK_MAIN();