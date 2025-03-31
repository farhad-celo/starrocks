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
2025-03-31T01:02:45+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_values_const_bench
Run on (32 X 3241.02 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.45, 6.48, 4.70
// Args: Number of rows / Number of possible strings / Size of old (new) maps
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_RemapValuesNonConstant/1000/40/20        54408 ns        54408 ns        13085 RowInvRate=54.4082ns
BM_RemapValuesNonConstant/10000/40/20      490228 ns       490206 ns         1392 RowInvRate=49.0206ns
BM_RemapValuesNonConstant/100000/40/20    5134098 ns      5133999 ns          144 RowInvRate=51.34ns
BM_RemapValuesNonConstant/1000/60/20        56095 ns        56096 ns        13083 RowInvRate=56.0962ns
BM_RemapValuesNonConstant/10000/60/20      502205 ns       502188 ns         1152 RowInvRate=50.2188ns
BM_RemapValuesNonConstant/100000/60/20    5190545 ns      5190620 ns          144 RowInvRate=51.9062ns
BM_RemapValuesNonConstant/1000/40/40        58836 ns        58836 ns        11822 RowInvRate=58.836ns
BM_RemapValuesNonConstant/10000/40/40      529765 ns       529731 ns         1325 RowInvRate=52.9731ns
BM_RemapValuesNonConstant/100000/40/40    5178308 ns      5178237 ns          133 RowInvRate=51.7824ns
BM_RemapValuesNonConstant/1000/60/40        59325 ns        59324 ns        11838 RowInvRate=59.3236ns
BM_RemapValuesNonConstant/10000/60/40      525281 ns       525268 ns         1347 RowInvRate=52.5268ns
BM_RemapValuesNonConstant/100000/60/40    5088575 ns      5088659 ns          132 RowInvRate=50.8866ns
BM_RemapValuesConstant/1000/40/20           48753 ns        48750 ns        14174 RowInvRate=48.7502ns
BM_RemapValuesConstant/10000/40/20         469843 ns       469819 ns         1496 RowInvRate=46.9819ns
BM_RemapValuesConstant/100000/40/20       5168106 ns      5167869 ns          100 RowInvRate=51.6787ns
BM_RemapValuesConstant/1000/60/20           48602 ns        48600 ns        14713 RowInvRate=48.6003ns
BM_RemapValuesConstant/10000/60/20         433775 ns       433771 ns         1466 RowInvRate=43.3771ns
BM_RemapValuesConstant/100000/60/20       4195900 ns      4195793 ns          166 RowInvRate=41.9579ns
BM_RemapValuesConstant/1000/40/40           54402 ns        54400 ns        12665 RowInvRate=54.3998ns
BM_RemapValuesConstant/10000/40/40         479832 ns       479808 ns         1443 RowInvRate=47.9808ns
BM_RemapValuesConstant/100000/40/40       4788652 ns      4788661 ns          144 RowInvRate=47.8866ns
BM_RemapValuesConstant/1000/60/40           53630 ns        53629 ns        13058 RowInvRate=53.6287ns
BM_RemapValuesConstant/10000/60/40         479581 ns       479554 ns         1470 RowInvRate=47.9554ns
BM_RemapValuesConstant/100000/60/40       4681447 ns      4680859 ns          150 RowInvRate=46.8086ns
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
                old_map_column->append_datum(gen_rand_array(map_size));
                old_map_column = ConstColumn::create(old_map_column, num_rows);
                new_map_column->append_datum(gen_rand_array(map_size));
                new_map_column = ConstColumn::create(new_map_column, num_rows);
                ctx->set_constant_columns({nullptr, nullptr, nullptr, nullptr});
                break;
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::prepare_const(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::prepare_const(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisRemapValues<TYPE_VARCHAR>::remap_values_const(ctx.get(), {input_column, old_map_column, new_map_column, default_column}).ok());
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
