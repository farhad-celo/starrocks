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
2025-03-27T15:05:21+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_crop_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.37, 4.69, 2.62
// Args: Number of rows / Array Length
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_CalcCropAllAll/1000/10                175662 ns       175611 ns         3980 RowInvRate=175.611ns
BM_CalcCropAllAll/10000/10              1684704 ns      1684659 ns          415 RowInvRate=168.466ns
BM_CalcCropAllAll/100000/10            18785533 ns     18785617 ns           37 RowInvRate=187.856ns
BM_CalcCropAllAll/1000/20                251232 ns       251207 ns         2792 RowInvRate=251.207ns
BM_CalcCropAllAll/10000/20              2453945 ns      2453887 ns          285 RowInvRate=245.389ns
BM_CalcCropAllAll/100000/20            29391086 ns     29390827 ns           21 RowInvRate=293.908ns
BM_CalcCropAllAll/1000/40                436679 ns       436642 ns         1688 RowInvRate=436.642ns
BM_CalcCropAllAll/10000/40              4048345 ns      4048284 ns          167 RowInvRate=404.828ns
BM_CalcCropAllAll/100000/40            47474395 ns     47472402 ns           15 RowInvRate=474.724ns

BM_CalcCropFirstLast/1000/10             395567 ns       395546 ns         1918 RowInvRate=395.546ns
BM_CalcCropFirstLast/10000/10           3763828 ns      3763599 ns          194 RowInvRate=376.36ns
BM_CalcCropFirstLast/100000/10         39745384 ns     39744611 ns           18 RowInvRate=397.446ns
BM_CalcCropFirstLast/1000/20             626085 ns       626008 ns         1140 RowInvRate=626.008ns
BM_CalcCropFirstLast/10000/20           6215970 ns      6215838 ns          116 RowInvRate=621.584ns
BM_CalcCropFirstLast/100000/20         62771077 ns     62769250 ns           11 RowInvRate=627.693ns
BM_CalcCropFirstLast/1000/40            1207903 ns      1207777 ns          580 RowInvRate=1.20778us
BM_CalcCropFirstLast/10000/40          12215521 ns     12215272 ns           58 RowInvRate=1.22153us
BM_CalcCropFirstLast/100000/40        128435194 ns    128431719 ns            5 RowInvRate=1.28432us

BM_CalcCropToNullAllAll/1000/10          312789 ns       312763 ns         2237 RowInvRate=312.763ns
BM_CalcCropToNullAllAll/10000/10        3933669 ns      3933754 ns          225 RowInvRate=393.375ns
BM_CalcCropToNullAllAll/100000/10      33222253 ns     33221350 ns           17 RowInvRate=332.214ns
BM_CalcCropToNullAllAll/1000/20          548476 ns       548433 ns         1309 RowInvRate=548.433ns
BM_CalcCropToNullAllAll/10000/20        5128947 ns      5128909 ns          136 RowInvRate=512.891ns
BM_CalcCropToNullAllAll/100000/20      60203851 ns     60203874 ns           12 RowInvRate=602.039ns
BM_CalcCropToNullAllAll/1000/40          906341 ns       906343 ns          768 RowInvRate=906.343ns
BM_CalcCropToNullAllAll/10000/40        9066516 ns      9066253 ns           77 RowInvRate=906.625ns
BM_CalcCropToNullAllAll/100000/40     105961197 ns    105958707 ns            7 RowInvRate=1059.59ns

BM_CalcCropToNullFirstLast/1000/10       429399 ns       429387 ns         1640 RowInvRate=429.387ns
BM_CalcCropToNullFirstLast/10000/10     4262733 ns      4262741 ns          165 RowInvRate=426.274ns
BM_CalcCropToNullFirstLast/100000/10   44165622 ns     44165039 ns           16 RowInvRate=441.65ns
BM_CalcCropToNullFirstLast/1000/20       726475 ns       726373 ns          962 RowInvRate=726.373ns
BM_CalcCropToNullFirstLast/10000/20     7171814 ns      7171738 ns          101 RowInvRate=717.174ns
BM_CalcCropToNullFirstLast/100000/20   79944765 ns     79940474 ns            8 RowInvRate=799.405ns
BM_CalcCropToNullFirstLast/1000/40      1402680 ns      1402534 ns          498 RowInvRate=1.40253us
BM_CalcCropToNullFirstLast/10000/40    14043604 ns     14043686 ns           51 RowInvRate=1.40437us
BM_CalcCropToNullFirstLast/100000/40  150733864 ns    150733808 ns            5 RowInvRate=1.50734us
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
