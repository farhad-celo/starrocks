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
2024-11-01T15:20:13+00:00
Running ./be/build_Release/src/bench/celonis/output/remap_values_bench
Run on (32 X 2879.51 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.45, 4.90, 3.46
// Args: Number of rows / Number of possible strings / Size of old (new) maps
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_RemapValuesNonConstant/1000/40/20      1326383 ns      1326232 ns          528 RowInvRate=1.32623us
BM_RemapValuesNonConstant/10000/40/20    13198074 ns     13196138 ns           53 RowInvRate=1.31961us
BM_RemapValuesNonConstant/100000/40/20  138115776 ns    138102470 ns            5 RowInvRate=1.38102us
BM_RemapValuesNonConstant/1000/60/20      1329007 ns      1328739 ns          522 RowInvRate=1.32874us
BM_RemapValuesNonConstant/10000/60/20    13268962 ns     13267697 ns           53 RowInvRate=1.32677us
BM_RemapValuesNonConstant/100000/60/20  138252171 ns    138232176 ns            5 RowInvRate=1.38232us
BM_RemapValuesNonConstant/1000/40/40      2403935 ns      2402796 ns          292 RowInvRate=2.4028us
BM_RemapValuesNonConstant/10000/40/40    24574811 ns     24571330 ns           29 RowInvRate=2.45713us
BM_RemapValuesNonConstant/100000/40/40  258430563 ns    258385908 ns            3 RowInvRate=2.58386us
BM_RemapValuesNonConstant/1000/60/40      2424225 ns      2423962 ns          289 RowInvRate=2.42396us
BM_RemapValuesNonConstant/10000/60/40    24793853 ns     24792131 ns           28 RowInvRate=2.47921us
BM_RemapValuesNonConstant/100000/60/40  259059275 ns    259037839 ns            3 RowInvRate=2.59038us
BM_RemapValuesConstant/1000/40/20           64152 ns        64131 ns        11185 RowInvRate=64.1309ns
BM_RemapValuesConstant/10000/40/20         592989 ns       592792 ns         1233 RowInvRate=59.2792ns
BM_RemapValuesConstant/100000/40/20       5701246 ns      5698563 ns          122 RowInvRate=56.9856ns
BM_RemapValuesConstant/1000/60/20           59477 ns        59472 ns        11654 RowInvRate=59.4722ns
BM_RemapValuesConstant/10000/60/20         535936 ns       535886 ns         1288 RowInvRate=53.5886ns
BM_RemapValuesConstant/100000/60/20       5184079 ns      5183607 ns          129 RowInvRate=51.8361ns
BM_RemapValuesConstant/1000/40/40           72608 ns        72605 ns         9757 RowInvRate=72.6048ns
BM_RemapValuesConstant/10000/40/40         666415 ns       666374 ns         1055 RowInvRate=66.6374ns
BM_RemapValuesConstant/100000/40/40       6432207 ns      6431332 ns          107 RowInvRate=64.3133ns
BM_RemapValuesConstant/1000/60/40           68377 ns        68365 ns         9912 RowInvRate=68.3649ns
BM_RemapValuesConstant/10000/60/40         626212 ns       626190 ns         1126 RowInvRate=62.619ns
BM_RemapValuesConstant/100000/60/40       6222981 ns      6222508 ns          111 RowInvRate=62.2251ns
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
