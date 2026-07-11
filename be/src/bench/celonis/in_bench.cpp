#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/in.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2024-01-19T20:01:36+00:00
Running ./be/build_Release/src/bench/celonis/output/in_bench
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.83, 4.27, 10.99
Args: Number of rows / Number of possible values / Size of match list
-------------------------------------------------------------------------------------------------------------
Benchmark                                                   Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------------
BM_InNonConstantVARCHAR/1000/20/5                      259291 ns       259228 ns         2697 RowInvRate=259.228ns
BM_InNonConstantVARCHAR/10000/20/5                    2561811 ns      2561652 ns          274 RowInvRate=256.165ns
BM_InNonConstantVARCHAR/100000/20/5                  25313299 ns     25309867 ns           28 RowInvRate=253.099ns
BM_InNonConstantVARCHAR/1000/40/5                      256184 ns       256112 ns         2699 RowInvRate=256.112ns
BM_InNonConstantVARCHAR/10000/40/5                    2526863 ns      2526348 ns          278 RowInvRate=252.635ns
BM_InNonConstantVARCHAR/100000/40/5                  24906728 ns     24902593 ns           28 RowInvRate=249.026ns
BM_InNonConstantVARCHAR/1000/60/5                      253687 ns       253613 ns         2767 RowInvRate=253.613ns
BM_InNonConstantVARCHAR/10000/60/5                    2495249 ns      2494897 ns          280 RowInvRate=249.49ns
BM_InNonConstantVARCHAR/100000/60/5                  24645085 ns     24640734 ns           28 RowInvRate=246.407ns
BM_InNonConstantVARCHAR/1000/20/10                     391609 ns       391444 ns         1784 RowInvRate=391.444ns
BM_InNonConstantVARCHAR/10000/20/10                   3898118 ns      3897729 ns          180 RowInvRate=389.773ns
BM_InNonConstantVARCHAR/100000/20/10                 39516776 ns     39510530 ns           18 RowInvRate=395.105ns
BM_InNonConstantVARCHAR/1000/40/10                     389378 ns       389191 ns         1804 RowInvRate=389.191ns
BM_InNonConstantVARCHAR/10000/40/10                   3875267 ns      3874613 ns          181 RowInvRate=387.461ns
BM_InNonConstantVARCHAR/100000/40/10                 39049559 ns     39043171 ns           18 RowInvRate=390.432ns
BM_InNonConstantVARCHAR/1000/60/10                     383504 ns       383311 ns         1823 RowInvRate=383.311ns
BM_InNonConstantVARCHAR/10000/60/10                   3829041 ns      3828562 ns          184 RowInvRate=382.856ns
BM_InNonConstantVARCHAR/100000/60/10                 38633265 ns     38629740 ns           18 RowInvRate=386.297ns
BM_InConstantVARCHAR/1000/20/5                          19962 ns        19955 ns        35099 RowInvRate=19.9548ns
BM_InConstantVARCHAR/10000/20/5                        186428 ns       186385 ns         3752 RowInvRate=18.6385ns
BM_InConstantVARCHAR/100000/20/5                      1845073 ns      1844850 ns          380 RowInvRate=18.4485ns
BM_InConstantVARCHAR/1000/40/5                          17050 ns        17045 ns        41087 RowInvRate=17.0452ns
BM_InConstantVARCHAR/10000/40/5                        156252 ns       156229 ns         4480 RowInvRate=15.6229ns
BM_InConstantVARCHAR/100000/40/5                      1538310 ns      1538117 ns          455 RowInvRate=15.3812ns
BM_InConstantVARCHAR/1000/60/5                          15485 ns        15481 ns        45171 RowInvRate=15.4809ns
BM_InConstantVARCHAR/10000/60/5                        142317 ns       142280 ns         4928 RowInvRate=14.228ns
BM_InConstantVARCHAR/100000/60/5                      1395170 ns      1395066 ns          498 RowInvRate=13.9507ns
BM_InConstantVARCHAR/1000/20/10                         22807 ns        22800 ns        30675 RowInvRate=22.7998ns
BM_InConstantVARCHAR/10000/20/10                       210042 ns       210031 ns         3342 RowInvRate=21.0031ns
BM_InConstantVARCHAR/100000/20/10                     2070983 ns      2070784 ns          337 RowInvRate=20.7078ns
BM_InConstantVARCHAR/1000/40/10                         19387 ns        19381 ns        36146 RowInvRate=19.3809ns
BM_InConstantVARCHAR/10000/40/10                       177853 ns       177799 ns         3939 RowInvRate=17.7799ns
BM_InConstantVARCHAR/100000/40/10                     1746370 ns      1746246 ns          402 RowInvRate=17.4625ns
BM_InConstantVARCHAR/1000/60/10                         17364 ns        17358 ns        40357 RowInvRate=17.3584ns
BM_InConstantVARCHAR/10000/60/10                       157857 ns       157845 ns         4429 RowInvRate=15.7845ns
BM_InConstantVARCHAR/100000/60/10                     1557082 ns      1556930 ns          449 RowInvRate=15.5693ns
BM_InConstantLargeMatchesVARCHAR/1000/5000/3000        187360 ns       187357 ns         3733 RowInvRate=187.357ns
BM_InConstantLargeMatchesVARCHAR/10000/5000/3000       367474 ns       367439 ns         1907 RowInvRate=36.7439ns
BM_InConstantLargeMatchesVARCHAR/100000/5000/3000     2162743 ns      2162634 ns          320 RowInvRate=21.6263ns
BM_InConstantLargeMatchesVARCHAR/1000000/5000/3000   28135377 ns     28132480 ns           25 RowInvRate=28.1325ns
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

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                        TypeDescriptor::from_logical_type(TYPE_ARRAY)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BOOLEAN);
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
            // Array Literal is not wrapped with ConstColumn.
            // As of 2024-01-30, it has one row in FunctionContext::constant_column_ and it is evaluated and unfolded to
            // multiple rows in /be/src/exprs/array_expr.cpp before it is passed to celonis_in().
            // In this benchmark, we don't unfold the column when we call the function as the function doesn't read it.
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
        ASSERT_TRUE(CelonisIn<TYPE_VARCHAR>::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisIn<TYPE_VARCHAR>::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisIn<TYPE_VARCHAR>::in(ctx.get(), {input_column, match_column}).ok());
        ASSERT_TRUE(CelonisIn<TYPE_VARCHAR>::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisIn<TYPE_VARCHAR>::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
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

static void BM_InConstantLargeMatchesVARCHAR(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

// Args: Number of rows / Number of possible values / Size of match list
BENCHMARK(BM_InNonConstantVARCHAR)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {5, 10}});
BENCHMARK(BM_InConstantVARCHAR)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {5, 10}});
BENCHMARK(BM_InConstantLargeMatchesVARCHAR)->ArgsProduct({{1000, 10000, 100000, 1000000}, {5000}, {3000}});

} // namespace starrocks

BENCHMARK_MAIN();