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
2026-07-23T23:30:14+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_crop_bench
Run on (32 X 2499.99 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 1024 KiB (x16)
  L3 Unified 36608 KiB (x1)
Load Average: 4.41, 3.00, 2.58
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_CalcCropAllAll/1000/10                152485 ns       152479 ns         4295 RowInvRate=152.479ns
BM_CalcCropAllAll/10000/10              1498610 ns      1498353 ns          462 RowInvRate=149.835ns
BM_CalcCropAllAll/100000/10            28948634 ns     28947962 ns           24 RowInvRate=289.48ns
BM_CalcCropAllAll/1000/20                288259 ns       288187 ns         2426 RowInvRate=288.187ns
BM_CalcCropAllAll/10000/20              2887426 ns      2887166 ns          237 RowInvRate=288.717ns
BM_CalcCropAllAll/100000/20            58811366 ns     58811170 ns           12 RowInvRate=588.112ns
BM_CalcCropAllAll/1000/40                535345 ns       535314 ns         1308 RowInvRate=535.314ns
BM_CalcCropAllAll/10000/40              5434372 ns      5434137 ns          126 RowInvRate=543.414ns
BM_CalcCropAllAll/100000/40           121860030 ns    121853823 ns            6 RowInvRate=1.21854us
BM_CalcCropFirstLast/1000/10             260009 ns       259964 ns         2693 RowInvRate=259.964ns
BM_CalcCropFirstLast/10000/10           2570643 ns      2570426 ns          268 RowInvRate=257.043ns
BM_CalcCropFirstLast/100000/10         37973246 ns     37971378 ns           19 RowInvRate=379.714ns
BM_CalcCropFirstLast/1000/20             463021 ns       462959 ns         1520 RowInvRate=462.959ns
BM_CalcCropFirstLast/10000/20           4618122 ns      4618031 ns          152 RowInvRate=461.803ns
BM_CalcCropFirstLast/100000/20         69579815 ns     69575922 ns           11 RowInvRate=695.759ns
BM_CalcCropFirstLast/1000/40             897078 ns       897004 ns          779 RowInvRate=897.004ns
BM_CalcCropFirstLast/10000/40           9191281 ns      9190722 ns           74 RowInvRate=919.072ns
BM_CalcCropFirstLast/100000/40        144678637 ns    144669314 ns            5 RowInvRate=1.44669us
BM_CalcCropToNullAllAll/1000/10          309985 ns       310015 ns         2258 RowInvRate=310.015ns
BM_CalcCropToNullAllAll/10000/10        3109005 ns      3107065 ns          225 RowInvRate=310.706ns
BM_CalcCropToNullAllAll/100000/10      47827775 ns     47824228 ns           13 RowInvRate=478.242ns
BM_CalcCropToNullAllAll/1000/20          593378 ns       593348 ns         1182 RowInvRate=593.348ns
BM_CalcCropToNullAllAll/10000/20        5924009 ns      5923931 ns          118 RowInvRate=592.393ns
BM_CalcCropToNullAllAll/100000/20     116999365 ns    116988128 ns            6 RowInvRate=1.16988us
BM_CalcCropToNullAllAll/1000/40         1131276 ns      1130974 ns          619 RowInvRate=1.13097us
BM_CalcCropToNullAllAll/10000/40       11661148 ns     11660123 ns           60 RowInvRate=1.16601us
BM_CalcCropToNullAllAll/100000/40     233836391 ns    233830158 ns            3 RowInvRate=2.3383us
BM_CalcCropToNullFirstLast/1000/10       348203 ns       348107 ns         2011 RowInvRate=348.107ns
BM_CalcCropToNullFirstLast/10000/10     3461304 ns      3460931 ns          202 RowInvRate=346.093ns
BM_CalcCropToNullFirstLast/100000/10   43517972 ns     43516977 ns           16 RowInvRate=435.17ns
BM_CalcCropToNullFirstLast/1000/20       617172 ns       617167 ns         1123 RowInvRate=617.167ns
BM_CalcCropToNullFirstLast/10000/20     6152572 ns      6152318 ns          111 RowInvRate=615.232ns
BM_CalcCropToNullFirstLast/100000/20   93809135 ns     93802310 ns            7 RowInvRate=938.023ns
BM_CalcCropToNullFirstLast/1000/40      1196164 ns      1195730 ns          575 RowInvRate=1.19573us
BM_CalcCropToNullFirstLast/10000/40    12204724 ns     12203373 ns           55 RowInvRate=1.22034us
BM_CalcCropToNullFirstLast/100000/40  179805339 ns    179802208 ns            3 RowInvRate=1.79802us
BM_CalcCropAllAllInt/1000/10             127978 ns       127931 ns         5477 RowInvRate=127.931ns
BM_CalcCropAllAllInt/10000/10           1245941 ns      1245752 ns          560 RowInvRate=124.575ns
BM_CalcCropAllAllInt/100000/10         16301428 ns     16300782 ns           43 RowInvRate=163.008ns
BM_CalcCropAllAllInt/1000/20             241139 ns       241127 ns         2906 RowInvRate=241.127ns
BM_CalcCropAllAllInt/10000/20           2389805 ns      2389015 ns          294 RowInvRate=238.902ns
BM_CalcCropAllAllInt/100000/20         32543477 ns     32540465 ns           21 RowInvRate=325.405ns
BM_CalcCropAllAllInt/1000/40             439287 ns       439214 ns         1593 RowInvRate=439.214ns
BM_CalcCropAllAllInt/10000/40           4398571 ns      4398279 ns          159 RowInvRate=439.828ns
BM_CalcCropAllAllInt/100000/40         63371263 ns     63367405 ns           11 RowInvRate=633.674ns
BM_CalcCropFirstLastInt/1000/10          145835 ns       145832 ns         4794 RowInvRate=145.832ns
BM_CalcCropFirstLastInt/10000/10        1418090 ns      1417598 ns          493 RowInvRate=141.76ns
BM_CalcCropFirstLastInt/100000/10      16184150 ns     16183081 ns           45 RowInvRate=161.831ns
BM_CalcCropFirstLastInt/1000/20          256524 ns       256497 ns         2726 RowInvRate=256.497ns
BM_CalcCropFirstLastInt/10000/20        2539241 ns      2538924 ns          278 RowInvRate=253.892ns
BM_CalcCropFirstLastInt/100000/20      29953365 ns     29952920 ns           23 RowInvRate=299.529ns
BM_CalcCropFirstLastInt/1000/40          462898 ns       462839 ns         1510 RowInvRate=462.839ns
BM_CalcCropFirstLastInt/10000/40        4615849 ns      4615698 ns          151 RowInvRate=461.57ns
BM_CalcCropFirstLastInt/100000/40      51737947 ns     51735741 ns           10 RowInvRate=517.357ns
*/

// element_type is the activity element type (args 1/2/4); the begin/end range
// modes (args 3/5) are always VARCHAR. Fn is the scalar function to benchmark
// (kept as a template parameter so both calc_crop<...> and calc_crop_to_null
// can be passed). Pass the matching calc_crop<element_type> for Fn.
template <LogicalType element_type, typename Fn>
static void bench(benchmark::State& state, Fn scalar_function, const std::string& begin_mode,
                  const std::string& end_mode) {
    int num_rows = state.range(0);
    int array_length = state.range(1);

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, array_length - 1);

    // Activity element TypeDescriptor for the columns below.
    const TypeDescriptor element_type_desc = [] {
        if constexpr (element_type == TYPE_VARCHAR) {
            return TypeDescriptor(TYPE_VARCHAR);
        } else {
            return TypeDescriptor(TYPE_INT);
        }
    }();

    // For VARCHAR we need a backing pool of strings that the generated Slices
    // reference; for INT the value is the index itself.
    [[maybe_unused]] std::vector<std::string> values;
    if constexpr (element_type == TYPE_VARCHAR) {
        values.reserve(array_length);
        for (int i = 0; i < array_length; i++) {
            std::string activity = std::to_string(i) + "-activity";
            values.push_back(activity);
        }
    }

    auto gen_element = [&](int idx) {
        if constexpr (element_type == TYPE_VARCHAR) {
            return Slice(values[idx]);
        } else {
            return static_cast<int32_t>(idx);
        }
    };

    DatumArray input_array;
    for (int j = 0; j < array_length; j++) {
        input_array.emplace_back(gen_element(j));
    }

    std::vector<FunctionContext::TypeDesc> arg_types = {
            TypeDescriptor::create_array_type(element_type_desc), element_type_desc,
            TypeDescriptor::from_logical_type(TYPE_VARCHAR), element_type_desc,
            TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), true);
        auto begin_activity_column = ColumnHelper::create_column(element_type_desc, true);
        auto begin_mode_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_activity_column = ColumnHelper::create_column(element_type_desc, true);
        auto end_mode_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        begin_activity_column->append_datum(gen_element(uniform_value(rng)));
        begin_activity_column = ConstColumn::create(begin_activity_column, num_rows);
        begin_mode_column->append_datum(Slice(begin_mode));
        begin_mode_column = ConstColumn::create(begin_mode_column, num_rows);

        end_activity_column->append_datum(gen_element(uniform_value(rng)));
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
    bench<TYPE_VARCHAR>(state, &(CelonisArrayFunctions::calc_crop<TYPE_VARCHAR>), "ALL", "ALL");
}

static void BM_CalcCropFirstLast(benchmark::State& state) {
    bench<TYPE_VARCHAR>(state, &CelonisArrayFunctions::calc_crop<TYPE_VARCHAR>, "FIRST", "LAST");
}

static void BM_CalcCropToNullAllAll(benchmark::State& state) {
    bench<TYPE_VARCHAR>(state, CelonisArrayFunctions::calc_crop_to_null, "ALL", "ALL");
}

static void BM_CalcCropToNullFirstLast(benchmark::State& state) {
    bench<TYPE_VARCHAR>(state, CelonisArrayFunctions::calc_crop_to_null, "FIRST", "LAST");
}

// INT-typed variants of calc_crop (element type passed as the template argument
// to calc_crop). calc_crop_to_null is intentionally not run with integers.
static void BM_CalcCropAllAllInt(benchmark::State& state) {
    bench<TYPE_INT>(state, &(CelonisArrayFunctions::calc_crop<TYPE_INT>), "ALL", "ALL");
}

static void BM_CalcCropFirstLastInt(benchmark::State& state) {
    bench<TYPE_INT>(state, &CelonisArrayFunctions::calc_crop<TYPE_INT>, "FIRST", "LAST");
}

// Args: Number of rows / Array Length
BENCHMARK(BM_CalcCropAllAll)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropFirstLast)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullAllAll)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullFirstLast)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropAllAllInt)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropFirstLastInt)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
