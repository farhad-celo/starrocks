#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/remap_values.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2024-10-01T15:30:20+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_values_bench
Run on (32 X 2879.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.28, 2.29, 3.60
// Args: Number of rows / Number of possible strings / Size of old (new) maps
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_RemapValuesNonConstant/1000/40/20      2341333 ns      2341138 ns          304 RowInvRate=2.34114us
BM_RemapValuesNonConstant/10000/40/20    22840128 ns     22837521 ns           30 RowInvRate=2.28375us
BM_RemapValuesNonConstant/100000/40/20  237836107 ns    237815976 ns            3 RowInvRate=2.37816us
BM_RemapValuesNonConstant/1000/60/20      2377026 ns      2376183 ns          299 RowInvRate=2.37618us
BM_RemapValuesNonConstant/10000/60/20    23417419 ns     23414480 ns           30 RowInvRate=2.34145us
BM_RemapValuesNonConstant/100000/60/20  242551900 ns    242509447 ns            3 RowInvRate=2.42509us
BM_RemapValuesNonConstant/1000/40/40      3957928 ns      3957658 ns          178 RowInvRate=3.95766us
BM_RemapValuesNonConstant/10000/40/40    39629607 ns     39625017 ns           18 RowInvRate=3.9625us
BM_RemapValuesNonConstant/100000/40/40  408654564 ns    408617412 ns            2 RowInvRate=4.08617us
BM_RemapValuesNonConstant/1000/60/40      4325713 ns      4323525 ns          163 RowInvRate=4.32353us
BM_RemapValuesNonConstant/10000/60/40    43153333 ns     43149112 ns           16 RowInvRate=4.31491us
BM_RemapValuesNonConstant/100000/60/40  439680118 ns    439634721 ns            2 RowInvRate=4.39635us
BM_RemapValuesConstant/1000/40/20           61540 ns        61537 ns        11220 RowInvRate=61.5367ns
BM_RemapValuesConstant/10000/40/20         586314 ns       586204 ns         1185 RowInvRate=58.6204ns
BM_RemapValuesConstant/100000/40/20       5714476 ns      5714080 ns          120 RowInvRate=57.1408ns
BM_RemapValuesConstant/1000/60/20           58949 ns        58947 ns        11899 RowInvRate=58.9471ns
BM_RemapValuesConstant/10000/60/20         550074 ns       549929 ns         1292 RowInvRate=54.9929ns
BM_RemapValuesConstant/100000/60/20       5387573 ns      5386821 ns          126 RowInvRate=53.8682ns
BM_RemapValuesConstant/1000/40/40           68722 ns        68720 ns        10047 RowInvRate=68.7196ns
BM_RemapValuesConstant/10000/40/40         633128 ns       633005 ns         1110 RowInvRate=63.3005ns
BM_RemapValuesConstant/100000/40/40       6286676 ns      6285254 ns          111 RowInvRate=62.8525ns
BM_RemapValuesConstant/1000/60/40           67165 ns        67164 ns        10473 RowInvRate=67.1637ns
BM_RemapValuesConstant/10000/60/40         606285 ns       606185 ns         1154 RowInvRate=60.6185ns
BM_RemapValuesConstant/100000/60/40       6032716 ns      6032046 ns          113 RowInvRate=60.3205ns
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
    int map_size = state.range(2);

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
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto default_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_rand_element());
            default_column->append_datum("NULL");
        }
        auto old_map_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        auto new_map_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        switch (match_type) {
            case CONSTANT:
                old_map_column->append_datum(gen_rand_array(map_size));
                old_map_column = ConstColumn::create(old_map_column, num_rows);
                new_map_column->append_datum(gen_rand_array(map_size));
                new_map_column = ConstColumn::create(new_map_column, num_rows);
                ctx->set_constant_columns({nullptr, old_map_column, new_map_column, nullptr});
                break;
            case NON_CONSTANT:
                for (int i = 0; i < num_rows; i++) {
                    old_map_column->append_datum(gen_rand_array(map_size));
                    new_map_column->append_datum(gen_rand_array(map_size));
                }
                ctx->set_constant_columns({nullptr, nullptr, nullptr, nullptr});
                break;
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::remap_values(ctx.get(), {input_column, old_map_column, new_map_column, default_column}).ok());
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_RemapValuesNonConstant(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

static void BM_RemapValuesConstant(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

// Args: Number of rows / Number of possible strings / Size of old (new) maps
BENCHMARK(BM_RemapValuesNonConstant)->ArgsProduct({{1000, 10000, 100000}, {40, 60}, {20, 40}});
BENCHMARK(BM_RemapValuesConstant)->ArgsProduct({{1000, 10000, 100000}, {40, 60}, {20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
