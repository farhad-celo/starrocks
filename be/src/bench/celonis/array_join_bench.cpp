#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/array_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-05-27T22:14:02+00:00
Running ./be/build_Release/src/bench/celonis/output/array_join_bench
Run on (32 X 3243.02 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.57, 3.84, 3.03
// Number of rows / Number of distinct activities / Variant length / NULL activity percentage
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
BM_ArrayJoin/10000/20/20/0       9030507 ns      9030391 ns           77 RowInvRate=903.039ns
BM_ArrayJoin/100000/20/20/0     92635622 ns     92635466 ns            8 RowInvRate=926.355ns
BM_ArrayJoin/10000/100/20/0      8574025 ns      8573534 ns           81 RowInvRate=857.353ns
BM_ArrayJoin/100000/100/20/0    88399427 ns     88396647 ns            8 RowInvRate=883.966ns
BM_ArrayJoin/10000/1000/20/0     8698052 ns      8697814 ns           81 RowInvRate=869.781ns
BM_ArrayJoin/100000/1000/20/0   89160191 ns     89158614 ns            8 RowInvRate=891.586ns
BM_ArrayJoin/10000/20/40/0      16661159 ns     16660362 ns           42 RowInvRate=1.66604us
BM_ArrayJoin/100000/20/40/0    171269444 ns    171261826 ns            4 RowInvRate=1.71262us
BM_ArrayJoin/10000/100/40/0     15742123 ns     15740936 ns           45 RowInvRate=1.57409us
BM_ArrayJoin/100000/100/40/0   162240581 ns    162230541 ns            4 RowInvRate=1.62231us
BM_ArrayJoin/10000/1000/40/0    15991492 ns     15990554 ns           44 RowInvRate=1.59906us
BM_ArrayJoin/100000/1000/40/0  164166947 ns    164163904 ns            4 RowInvRate=1.64164us
BM_ArrayJoin/10000/20/20/5      10908479 ns     10907237 ns           64 RowInvRate=1090.72ns
BM_ArrayJoin/100000/20/20/5    110393137 ns    110385172 ns            6 RowInvRate=1.10385us
BM_ArrayJoin/10000/100/20/5     10467605 ns     10465614 ns           67 RowInvRate=1046.56ns
BM_ArrayJoin/100000/100/20/5   107052519 ns    107051506 ns            7 RowInvRate=1070.52ns
BM_ArrayJoin/10000/1000/20/5    10623894 ns     10623801 ns           66 RowInvRate=1062.38ns
BM_ArrayJoin/100000/1000/20/5  108113478 ns    108110104 ns            6 RowInvRate=1081.1ns
BM_ArrayJoin/10000/20/40/5      20240744 ns     20239891 ns           35 RowInvRate=2.02399us
BM_ArrayJoin/100000/20/40/5    203005255 ns    202998742 ns            3 RowInvRate=2.02999us
BM_ArrayJoin/10000/100/40/5     18956883 ns     18956161 ns           37 RowInvRate=1.89562us
BM_ArrayJoin/100000/100/40/5   198454978 ns    198453186 ns            4 RowInvRate=1.98453us
BM_ArrayJoin/10000/1000/40/5    19695222 ns     19694328 ns           35 RowInvRate=1.96943us
BM_ArrayJoin/100000/1000/40/5  201721456 ns    201708289 ns            3 RowInvRate=2.01708us
*/

static void do_bench(benchmark::State& state) {
    const int num_rows = state.range(0);
    const int num_distinct_activities = state.range(1);
    const int variant_length = state.range(2);
    double null_probability = state.range(3) / 100.0;

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_distinct_activities - 1);
    std::bernoulli_distribution null_dist(null_probability);

    std::vector<std::string> values;
    values.reserve(num_distinct_activities);
    for (int i = 0; i < num_distinct_activities; i++) {
        values.push_back("Activity" + std::to_string(i));
    }

    auto gen_rand_element = [&]() { return Slice(values[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            bool is_null = null_dist(rng);
            if (is_null) {
                array.emplace_back(kNullDatum);
            } else {
                array.emplace_back(gen_rand_element());
            }
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto variant_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto sep_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            variant_column->append_datum(gen_rand_array(variant_length));
            sep_column->append_datum(", ");
        }
        ctx->set_constant_columns({nullptr, nullptr});

        state.ResumeTiming();
        ASSERT_TRUE(ArrayFunctions::array_join(ctx.get(), {variant_column, sep_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ArrayJoin(benchmark::State& state) {
    do_bench(state);
}

// Number of rows / Number of distinct activities / Variant length / NULL activity percentage
BENCHMARK(BM_ArrayJoin)->ArgsProduct({{10000, 100000}, {20, 100, 1000}, {20, 40}, {0, 5}});

} // namespace starrocks

BENCHMARK_MAIN();
