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
2025-03-28T21:38:06+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_crop_bench
Run on (32 X 3241.36 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 9.92, 8.61, 7.48
// Args: Number of rows / Array Length
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_CalcCropAllAll/1000/10                106544 ns       106515 ns         6612 RowInvRate=106.515ns
BM_CalcCropAllAll/10000/10               995596 ns       995583 ns          704 RowInvRate=99.5583ns
BM_CalcCropAllAll/100000/10            12033376 ns     12033518 ns           58 RowInvRate=120.335ns
BM_CalcCropAllAll/1000/20                183288 ns       183269 ns         3875 RowInvRate=183.269ns
BM_CalcCropAllAll/10000/20              1752623 ns      1752605 ns          379 RowInvRate=175.261ns
BM_CalcCropAllAll/100000/20            22515220 ns     22514710 ns           33 RowInvRate=225.147ns
BM_CalcCropAllAll/1000/40                335323 ns       335312 ns         2080 RowInvRate=335.312ns
BM_CalcCropAllAll/10000/40              3317356 ns      3317227 ns          215 RowInvRate=331.723ns
BM_CalcCropAllAll/100000/40            40689986 ns     40689355 ns           17 RowInvRate=406.894ns

BM_CalcCropFirstLast/1000/10             331591 ns       331564 ns         2116 RowInvRate=331.564ns
BM_CalcCropFirstLast/10000/10           3355245 ns      3355265 ns          211 RowInvRate=335.527ns
BM_CalcCropFirstLast/100000/10         35140459 ns     35139827 ns           20 RowInvRate=351.398ns
BM_CalcCropFirstLast/1000/20             576083 ns       575855 ns         1224 RowInvRate=575.855ns
BM_CalcCropFirstLast/10000/20           5666673 ns      5666596 ns          126 RowInvRate=566.66ns
BM_CalcCropFirstLast/100000/20         58950170 ns     58947797 ns           12 RowInvRate=589.478ns
BM_CalcCropFirstLast/1000/40            1144238 ns      1144039 ns          611 RowInvRate=1.14404us
BM_CalcCropFirstLast/10000/40          11681807 ns     11681730 ns           60 RowInvRate=1.16817us
BM_CalcCropFirstLast/100000/40        119435216 ns    119432698 ns            6 RowInvRate=1.19433us

BM_CalcCropToNullAllAll/1000/10          210720 ns       210680 ns         3322 RowInvRate=210.68ns
BM_CalcCropToNullAllAll/10000/10        2042746 ns      2042640 ns          344 RowInvRate=204.264ns
BM_CalcCropToNullAllAll/100000/10      22818095 ns     22818065 ns           29 RowInvRate=228.181ns
BM_CalcCropToNullAllAll/1000/20          401819 ns       401805 ns         1756 RowInvRate=401.805ns
BM_CalcCropToNullAllAll/10000/20        4003038 ns      4002950 ns          176 RowInvRate=400.295ns
BM_CalcCropToNullAllAll/100000/20      46955656 ns     46955684 ns           15 RowInvRate=469.557ns
BM_CalcCropToNullAllAll/1000/40          743373 ns       743259 ns          947 RowInvRate=743.259ns
BM_CalcCropToNullAllAll/10000/40        7511412 ns      7511474 ns           93 RowInvRate=751.147ns
BM_CalcCropToNullAllAll/100000/40      91297874 ns     91295750 ns            8 RowInvRate=912.957ns

BM_CalcCropToNullFirstLast/1000/10       397157 ns       397155 ns         1760 RowInvRate=397.155ns
BM_CalcCropToNullFirstLast/10000/10     3865196 ns      3865108 ns          180 RowInvRate=386.511ns
BM_CalcCropToNullFirstLast/100000/10   40820423 ns     40820516 ns           17 RowInvRate=408.205ns
BM_CalcCropToNullFirstLast/1000/20       697809 ns       697768 ns         1016 RowInvRate=697.768ns
BM_CalcCropToNullFirstLast/10000/20     6956093 ns      6955897 ns           98 RowInvRate=695.59ns
BM_CalcCropToNullFirstLast/100000/20   72605519 ns     72605420 ns            9 RowInvRate=726.054ns
BM_CalcCropToNullFirstLast/1000/40      1426281 ns      1426231 ns          498 RowInvRate=1.42623us
BM_CalcCropToNullFirstLast/10000/40    13929618 ns     13929530 ns           49 RowInvRate=1.39295us
BM_CalcCropToNullFirstLast/100000/40  152507679 ns    152505615 ns            5 RowInvRate=1.52506us
*/

using ScalarFunction = StatusOr<ColumnPtr> (*)(FunctionContext* context, const Columns& columns);

static void bench(benchmark::State& state, ScalarFunction scalar_function,
        const std::string& begin_mode, const std::string& end_mode) {
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
