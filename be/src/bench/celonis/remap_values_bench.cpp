#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/remap_values.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-31T01:09:33+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_values_bench
Run on (32 X 3242.97 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 13.93, 9.66, 6.52
// Args: Number of rows / Number of possible strings / Size of old (new) maps
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_RemapValuesNonConstant/1000/40/20      1427486 ns      1427304 ns          483 RowInvRate=1.4273us
BM_RemapValuesNonConstant/10000/40/20    14402269 ns     14401721 ns           50 RowInvRate=1.44017us
BM_RemapValuesNonConstant/100000/40/20  145888841 ns    145887114 ns            5 RowInvRate=1.45887us
BM_RemapValuesNonConstant/1000/60/20      1397979 ns      1397922 ns          500 RowInvRate=1.39792us
BM_RemapValuesNonConstant/10000/60/20    13861571 ns     13861377 ns           50 RowInvRate=1.38614us
BM_RemapValuesNonConstant/100000/60/20  139015420 ns    139009539 ns            5 RowInvRate=1.3901us
BM_RemapValuesNonConstant/1000/40/40      2575109 ns      2574893 ns          274 RowInvRate=2.57489us
BM_RemapValuesNonConstant/10000/40/40    25024592 ns     25023331 ns           28 RowInvRate=2.50233us
BM_RemapValuesNonConstant/100000/40/40  257864507 ns    257855546 ns            3 RowInvRate=2.57856us
BM_RemapValuesNonConstant/1000/60/40      2624097 ns      2623984 ns          271 RowInvRate=2.62398us
BM_RemapValuesNonConstant/10000/60/40    25492787 ns     25491253 ns           27 RowInvRate=2.54913us
BM_RemapValuesNonConstant/100000/60/40  257203854 ns    257200900 ns            3 RowInvRate=2.57201us
BM_RemapValuesConstant/1000/40/20           49887 ns        49878 ns        13999 RowInvRate=49.8784ns
BM_RemapValuesConstant/10000/40/20         440697 ns       440681 ns         1559 RowInvRate=44.0681ns
BM_RemapValuesConstant/100000/40/20       4394037 ns      4393944 ns          156 RowInvRate=43.9394ns
BM_RemapValuesConstant/1000/60/20           47382 ns        47379 ns        15099 RowInvRate=47.3791ns
BM_RemapValuesConstant/10000/60/20         426517 ns       426516 ns         1643 RowInvRate=42.6516ns
BM_RemapValuesConstant/100000/60/20       4193314 ns      4193209 ns          167 RowInvRate=41.9321ns
BM_RemapValuesConstant/1000/40/40           53990 ns        53991 ns        13023 RowInvRate=53.9913ns
BM_RemapValuesConstant/10000/40/40         478497 ns       478485 ns         1453 RowInvRate=47.8485ns
BM_RemapValuesConstant/100000/40/40       4780264 ns      4780136 ns          149 RowInvRate=47.8014ns
BM_RemapValuesConstant/1000/60/40           53772 ns        53773 ns        12873 RowInvRate=53.7734ns
BM_RemapValuesConstant/10000/60/40         481072 ns       481075 ns         1491 RowInvRate=48.1075ns
BM_RemapValuesConstant/100000/60/40       4664196 ns      4664148 ns          150 RowInvRate=46.6415ns
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
            TypeDescriptor::from_logical_type(TYPE_VARCHAR), TypeDescriptor::from_logical_type(TYPE_ARRAY),
            TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_VARCHAR);
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
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::remap_values(
                            ctx.get(), {input_column, old_map_column, new_map_column, default_column})
                            .ok());
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
