#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_functions.cpp"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-09-14T22:12:17+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_crop_bench
Run on (32 X 3245.5 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 3.19, 1.69, 1.17
// Args: Number of rows / Array Length
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_CalcCropAllAll/1000/10                103844 ns       103824 ns         6679 RowInvRate=103.824ns
BM_CalcCropAllAll/10000/10               994866 ns       994815 ns          712 RowInvRate=99.4815ns
BM_CalcCropAllAll/100000/10            11651687 ns     11650957 ns           60 RowInvRate=116.51ns
BM_CalcCropAllAll/1000/20                179862 ns       179808 ns         3902 RowInvRate=179.808ns
BM_CalcCropAllAll/10000/20              1938285 ns      1937310 ns          400 RowInvRate=193.731ns
BM_CalcCropAllAll/100000/20            21450770 ns     21450251 ns           33 RowInvRate=214.503ns
BM_CalcCropAllAll/1000/40                348629 ns       348539 ns         2057 RowInvRate=348.539ns
BM_CalcCropAllAll/10000/40              3275723 ns      3275552 ns          214 RowInvRate=327.555ns
BM_CalcCropAllAll/100000/40            39298764 ns     39295396 ns           18 RowInvRate=392.954ns

BM_CalcCropFirstLast/1000/10             243607 ns       243599 ns         2890 RowInvRate=243.599ns
BM_CalcCropFirstLast/10000/10           2343223 ns      2343103 ns          298 RowInvRate=234.31ns
BM_CalcCropFirstLast/100000/10         25417985 ns     25417202 ns           27 RowInvRate=254.172ns
BM_CalcCropFirstLast/1000/20             440352 ns       440308 ns         1602 RowInvRate=440.308ns
BM_CalcCropFirstLast/10000/20           4273845 ns      4273737 ns          166 RowInvRate=427.374ns
BM_CalcCropFirstLast/100000/20         46416448 ns     46414293 ns           16 RowInvRate=464.143ns
BM_CalcCropFirstLast/1000/40             853304 ns       853251 ns          813 RowInvRate=853.251ns
BM_CalcCropFirstLast/10000/40           8248778 ns      8248772 ns           85 RowInvRate=824.877ns
BM_CalcCropFirstLast/100000/40         87511630 ns     87509870 ns            8 RowInvRate=875.099ns

BM_CalcCropToNullAllAll/1000/10          212937 ns       212943 ns         3265 RowInvRate=212.943ns
BM_CalcCropToNullAllAll/10000/10        2059556 ns      2059589 ns          339 RowInvRate=205.959ns
BM_CalcCropToNullAllAll/100000/10      25104726 ns     25105263 ns           28 RowInvRate=251.053ns
BM_CalcCropToNullAllAll/1000/20          399735 ns       399708 ns         1755 RowInvRate=399.708ns
BM_CalcCropToNullAllAll/10000/20        3872763 ns      3872603 ns          177 RowInvRate=387.26ns
BM_CalcCropToNullAllAll/100000/20      48794767 ns     48790001 ns           14 RowInvRate=487.9ns
BM_CalcCropToNullAllAll/1000/40          754648 ns       754453 ns          929 RowInvRate=754.453ns
BM_CalcCropToNullAllAll/10000/40        7490427 ns      7490556 ns           95 RowInvRate=749.056ns
BM_CalcCropToNullAllAll/100000/40      91621381 ns     91618794 ns            8 RowInvRate=916.188ns

BM_CalcCropToNullFirstLast/1000/10       306154 ns       306138 ns         2284 RowInvRate=306.138ns
BM_CalcCropToNullFirstLast/10000/10     2949495 ns      2949466 ns          236 RowInvRate=294.947ns
BM_CalcCropToNullFirstLast/100000/10   31356059 ns     31351757 ns           22 RowInvRate=313.518ns
BM_CalcCropToNullFirstLast/1000/20       566443 ns       566282 ns         1233 RowInvRate=566.282ns
BM_CalcCropToNullFirstLast/10000/20     5564754 ns      5564499 ns          126 RowInvRate=556.45ns
BM_CalcCropToNullFirstLast/100000/20   59342231 ns     59337271 ns           12 RowInvRate=593.373ns
BM_CalcCropToNullFirstLast/1000/40      1087052 ns      1086553 ns          650 RowInvRate=1086.55ns
BM_CalcCropToNullFirstLast/10000/40    10642852 ns     10642344 ns           64 RowInvRate=1064.23ns
BM_CalcCropToNullFirstLast/100000/40  113311574 ns    113310947 ns            6 RowInvRate=1.13311us
*/

using ScalarFunction = StatusOr<ColumnPtr> (*)(FunctionContext* context, const Columns& columns);

static void bench(benchmark::State& state, ScalarFunction scalar_function, const std::string& begin_mode,
                  const std::string& end_mode) {
    int num_rows = state.range(0);
    int array_length = state.range(1);

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, array_length - 1);

    std::vector<std::string> values;
    values.reserve(array_length);
    for (int i = 0; i < array_length; i++) {
        std::string activity = std::to_string(i) + "-activity";
        values.push_back(activity);
    }

    DatumArray input_array;
    for (int j = 0; j < array_length; j++) {
        input_array.emplace_back(Slice(values[j]));
    }

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type =
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT)));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        auto begin_activity_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_activity_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        begin_activity_column->append_datum(Slice(values[uniform_value(rng)]));
        begin_activity_column = ConstColumn::create(begin_activity_column, num_rows);
        begin_mode_column->append_datum(Slice(begin_mode));
        begin_mode_column = ConstColumn::create(begin_mode_column, num_rows);

        end_activity_column->append_datum(Slice(values[uniform_value(rng)]));
        end_activity_column = ConstColumn::create(end_activity_column, num_rows);
        end_mode_column->append_datum(Slice(end_mode));
        end_mode_column = ConstColumn::create(end_mode_column, num_rows);

        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = scalar_function(ctx.get(), {input_column, begin_activity_column, begin_mode_column,
                                                  end_activity_column, end_mode_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_CalcCropAllAll(benchmark::State& state) {
    bench(state, CelonisArrayFunctions::calc_crop, "ALL", "ALL");
}

static void BM_CalcCropFirstLast(benchmark::State& state) {
    bench(state, CelonisArrayFunctions::calc_crop, "FIRST", "LAST");
}

static void BM_CalcCropToNullAllAll(benchmark::State& state) {
    bench(state, CelonisArrayFunctions::calc_crop_to_null, "ALL", "ALL");
}

static void BM_CalcCropToNullFirstLast(benchmark::State& state) {
    bench(state, CelonisArrayFunctions::calc_crop_to_null, "FIRST", "LAST");
}

// Args: Number of rows / Array Length
BENCHMARK(BM_CalcCropAllAll)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropFirstLast)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullAllAll)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullFirstLast)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
