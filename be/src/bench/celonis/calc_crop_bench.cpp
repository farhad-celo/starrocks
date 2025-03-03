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
2025-03-02T20:58:14+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_crop_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 8.50, 8.08, 5.26
// Args: Number of rows / Array Length
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_CalcCropAllAll/1000/10                249809 ns       249808 ns         2800 RowInvRate=249.808ns
BM_CalcCropAllAll/10000/10              2447848 ns      2447675 ns          288 RowInvRate=244.768ns
BM_CalcCropAllAll/100000/10            26291969 ns     26291934 ns           27 RowInvRate=262.919ns
BM_CalcCropAllAll/1000/20                399254 ns       399245 ns         1753 RowInvRate=399.245ns
BM_CalcCropAllAll/10000/20              3948210 ns      3948130 ns          178 RowInvRate=394.813ns
BM_CalcCropAllAll/100000/20            43191827 ns     43189167 ns           16 RowInvRate=431.892ns
BM_CalcCropAllAll/1000/40                692569 ns       692387 ns         1015 RowInvRate=692.387ns
BM_CalcCropAllAll/10000/40              6855504 ns      6855306 ns          102 RowInvRate=685.531ns
BM_CalcCropAllAll/100000/40            76582038 ns     76577045 ns            9 RowInvRate=765.77ns

BM_CalcCropFirstLast/1000/10             358596 ns       358572 ns         1952 RowInvRate=358.572ns
BM_CalcCropFirstLast/10000/10           3543860 ns      3543582 ns          200 RowInvRate=354.358ns
BM_CalcCropFirstLast/100000/10         37526883 ns     37524626 ns           19 RowInvRate=375.246ns
BM_CalcCropFirstLast/1000/20             593023 ns       592887 ns         1163 RowInvRate=592.887ns
BM_CalcCropFirstLast/10000/20           5837640 ns      5837291 ns          122 RowInvRate=583.729ns
BM_CalcCropFirstLast/100000/20         61469229 ns     61466561 ns           10 RowInvRate=614.666ns
BM_CalcCropFirstLast/1000/40            1149656 ns      1149619 ns          613 RowInvRate=1.14962us
BM_CalcCropFirstLast/10000/40          11398964 ns     11397945 ns           62 RowInvRate=1.13979us
BM_CalcCropFirstLast/100000/40        123134142 ns    123131149 ns            6 RowInvRate=1.23131us

BM_CalcCropToNullAllAll/1000/10          358093 ns       358119 ns         1953 RowInvRate=358.119ns
BM_CalcCropToNullAllAll/10000/10        3513537 ns      3513277 ns          199 RowInvRate=351.328ns
BM_CalcCropToNullAllAll/100000/10      37354717 ns     37353803 ns           19 RowInvRate=373.538ns
BM_CalcCropToNullAllAll/1000/20          621571 ns       621488 ns         1129 RowInvRate=621.488ns
BM_CalcCropToNullAllAll/10000/20        6090305 ns      6089968 ns          114 RowInvRate=608.997ns
BM_CalcCropToNullAllAll/100000/20      70991825 ns     70984471 ns           10 RowInvRate=709.845ns
BM_CalcCropToNullAllAll/1000/40         1125313 ns      1125039 ns          620 RowInvRate=1.12504us
BM_CalcCropToNullAllAll/10000/40       11021809 ns     11021382 ns           63 RowInvRate=1.10214us
BM_CalcCropToNullAllAll/100000/40     127846436 ns    127838854 ns            5 RowInvRate=1.27839us

BM_CalcCropToNullFirstLast/1000/10       410624 ns       410563 ns         1712 RowInvRate=410.563ns
BM_CalcCropToNullFirstLast/10000/10     4006423 ns      4006051 ns          174 RowInvRate=400.605ns
BM_CalcCropToNullFirstLast/100000/10   42043249 ns     42042156 ns           17 RowInvRate=420.422ns
BM_CalcCropToNullFirstLast/1000/20       687839 ns       687807 ns         1026 RowInvRate=687.807ns
BM_CalcCropToNullFirstLast/10000/20     6748329 ns      6748259 ns          105 RowInvRate=674.826ns
BM_CalcCropToNullFirstLast/100000/20   73736828 ns     73725446 ns            9 RowInvRate=737.254ns
BM_CalcCropToNullFirstLast/1000/40      1336092 ns      1335984 ns          527 RowInvRate=1.33598us
BM_CalcCropToNullFirstLast/10000/40    13318471 ns     13318276 ns           52 RowInvRate=1.33183us
BM_CalcCropToNullFirstLast/100000/40  137350969 ns    137347044 ns            5 RowInvRate=1.37347us
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
