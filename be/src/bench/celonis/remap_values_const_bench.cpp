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
2024-11-01T18:29:31+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_values_const_bench
Run on (32 X 2884.83 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.32, 1.69, 2.24
// Args: Number of rows / Number of possible strings / Size of old (new) maps
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_RemapValuesNonConstant/1000/40/20        66064 ns        66069 ns        10793 RowInvRate=66.0691ns
BM_RemapValuesNonConstant/10000/40/20      608411 ns       608363 ns         1176 RowInvRate=60.8363ns
BM_RemapValuesNonConstant/100000/40/20    6099340 ns      6098759 ns          115 RowInvRate=60.9876ns
BM_RemapValuesNonConstant/1000/60/20        61951 ns        61950 ns        11317 RowInvRate=61.9503ns
BM_RemapValuesNonConstant/10000/60/20      592119 ns       592070 ns         1221 RowInvRate=59.207ns
BM_RemapValuesNonConstant/100000/60/20    5833649 ns      5833032 ns          120 RowInvRate=58.3303ns
BM_RemapValuesNonConstant/1000/40/40        74445 ns        74453 ns         9318 RowInvRate=74.453ns
BM_RemapValuesNonConstant/10000/40/40      690921 ns       690842 ns          993 RowInvRate=69.0842ns
BM_RemapValuesNonConstant/100000/40/40    6814725 ns      6814221 ns          102 RowInvRate=68.1422ns
BM_RemapValuesNonConstant/1000/60/40        71223 ns        71227 ns         9819 RowInvRate=71.2266ns
BM_RemapValuesNonConstant/10000/60/40      653157 ns       653065 ns         1096 RowInvRate=65.3065ns
BM_RemapValuesNonConstant/100000/60/40    6409017 ns      6408294 ns          109 RowInvRate=64.0829ns
BM_RemapValuesConstant/1000/40/20           64961 ns        64946 ns        10482 RowInvRate=64.9458ns
BM_RemapValuesConstant/10000/40/20         613654 ns       613537 ns         1129 RowInvRate=61.3537ns
BM_RemapValuesConstant/100000/40/20       6030929 ns      6029845 ns          112 RowInvRate=60.2984ns
BM_RemapValuesConstant/1000/60/20           61126 ns        61120 ns        11300 RowInvRate=61.1204ns
BM_RemapValuesConstant/10000/60/20         571083 ns       571045 ns         1207 RowInvRate=57.1045ns
BM_RemapValuesConstant/100000/60/20       5703267 ns      5702691 ns          124 RowInvRate=57.0269ns
BM_RemapValuesConstant/1000/40/40           76630 ns        76636 ns         9219 RowInvRate=76.6363ns
BM_RemapValuesConstant/10000/40/40         704594 ns       704541 ns         1006 RowInvRate=70.4541ns
BM_RemapValuesConstant/100000/40/40       7035127 ns      7034410 ns           97 RowInvRate=70.3441ns
BM_RemapValuesConstant/1000/60/40           71505 ns        71506 ns         9680 RowInvRate=71.5062ns
BM_RemapValuesConstant/10000/60/40         669379 ns       669300 ns         1054 RowInvRate=66.93ns
BM_RemapValuesConstant/100000/60/40       6597392 ns      6596599 ns          108 RowInvRate=65.966ns
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
