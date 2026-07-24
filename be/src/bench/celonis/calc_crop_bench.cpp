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
2026-07-24T00:25:05+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_crop_bench
Run on (32 X 2499.99 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 1024 KiB (x16)
  L3 Unified 36608 KiB (x1)
Load Average: 6.29, 5.75, 3.34
--------------------------------------------------------------------------------------------------
Benchmark                                        Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------
BM_CalcCropAllAll/1000/10                   150474 ns       150462 ns         4625 RowInvRate=150.462ns
BM_CalcCropAllAll/10000/10                 1483893 ns      1483676 ns          472 RowInvRate=148.368ns
BM_CalcCropAllAll/100000/10               28875456 ns     28875153 ns           24 RowInvRate=288.752ns
BM_CalcCropAllAll/1000/20                   286859 ns       286706 ns         2465 RowInvRate=286.706ns
BM_CalcCropAllAll/10000/20                 2852993 ns      2852774 ns          247 RowInvRate=285.277ns
BM_CalcCropAllAll/100000/20               59047143 ns     59043994 ns           12 RowInvRate=590.44ns
BM_CalcCropAllAll/1000/40                   527749 ns       527349 ns         1327 RowInvRate=527.349ns
BM_CalcCropAllAll/10000/40                 5370283 ns      5369967 ns          128 RowInvRate=536.997ns
BM_CalcCropAllAll/100000/40              115632102 ns    115631568 ns            6 RowInvRate=1.15632us
BM_CalcCropFirstLast/1000/10                259820 ns       259805 ns         2688 RowInvRate=259.805ns
BM_CalcCropFirstLast/10000/10              2577940 ns      2577747 ns          268 RowInvRate=257.775ns
BM_CalcCropFirstLast/100000/10            38178090 ns     38177232 ns           18 RowInvRate=381.772ns
BM_CalcCropFirstLast/1000/20                447688 ns       447623 ns         1562 RowInvRate=447.623ns
BM_CalcCropFirstLast/10000/20              4466048 ns      4465985 ns          157 RowInvRate=446.599ns
BM_CalcCropFirstLast/100000/20            69273477 ns     69272073 ns            9 RowInvRate=692.721ns
BM_CalcCropFirstLast/1000/40                874043 ns       873835 ns          804 RowInvRate=873.835ns
BM_CalcCropFirstLast/10000/40              8972579 ns      8972465 ns           75 RowInvRate=897.247ns
BM_CalcCropFirstLast/100000/40           143334951 ns    143332582 ns            5 RowInvRate=1.43333us
BM_CalcCropToNullAllAll/1000/10             316654 ns       316668 ns         2210 RowInvRate=316.668ns
BM_CalcCropToNullAllAll/10000/10           3187187 ns      3186098 ns          219 RowInvRate=318.61ns
BM_CalcCropToNullAllAll/100000/10         47743192 ns     47743053 ns           13 RowInvRate=477.431ns
BM_CalcCropToNullAllAll/1000/20             600800 ns       600913 ns         1162 RowInvRate=600.913ns
BM_CalcCropToNullAllAll/10000/20           6021512 ns      6020408 ns          117 RowInvRate=602.041ns
BM_CalcCropToNullAllAll/100000/20        117788089 ns    117787553 ns            6 RowInvRate=1.17788us
BM_CalcCropToNullAllAll/1000/40            1188361 ns      1187902 ns          611 RowInvRate=1.1879us
BM_CalcCropToNullAllAll/10000/40          11739939 ns     11739243 ns           55 RowInvRate=1.17392us
BM_CalcCropToNullAllAll/100000/40        239384401 ns    239371586 ns            3 RowInvRate=2.39372us
BM_CalcCropToNullFirstLast/1000/10          345275 ns       345086 ns         2028 RowInvRate=345.086ns
BM_CalcCropToNullFirstLast/10000/10        3464773 ns      3464646 ns          207 RowInvRate=346.465ns
BM_CalcCropToNullFirstLast/100000/10      44498021 ns     44497161 ns           16 RowInvRate=444.972ns
BM_CalcCropToNullFirstLast/1000/20          605240 ns       605278 ns         1182 RowInvRate=605.278ns
BM_CalcCropToNullFirstLast/10000/20        6049740 ns      6049443 ns          113 RowInvRate=604.944ns
BM_CalcCropToNullFirstLast/100000/20      87713900 ns     87705170 ns            8 RowInvRate=877.052ns
BM_CalcCropToNullFirstLast/1000/40         1171254 ns      1171128 ns          592 RowInvRate=1.17113us
BM_CalcCropToNullFirstLast/10000/40       12070156 ns     12069607 ns           56 RowInvRate=1.20696us
BM_CalcCropToNullFirstLast/100000/40     163822672 ns    163819750 ns            4 RowInvRate=1.6382us
BM_CalcCropAllAllInt/1000/10                127402 ns       127388 ns         5508 RowInvRate=127.388ns
BM_CalcCropAllAllInt/10000/10              1236251 ns      1235739 ns          563 RowInvRate=123.574ns
BM_CalcCropAllAllInt/100000/10            16198251 ns     16197559 ns           43 RowInvRate=161.976ns
BM_CalcCropAllAllInt/1000/20                247291 ns       247188 ns         2919 RowInvRate=247.188ns
BM_CalcCropAllAllInt/10000/20              2376241 ns      2376192 ns          286 RowInvRate=237.619ns
BM_CalcCropAllAllInt/100000/20            32443278 ns     32442585 ns           20 RowInvRate=324.426ns
BM_CalcCropAllAllInt/1000/40                439585 ns       439529 ns         1557 RowInvRate=439.529ns
BM_CalcCropAllAllInt/10000/40              4422795 ns      4422726 ns          153 RowInvRate=442.273ns
BM_CalcCropAllAllInt/100000/40            63209989 ns     63208957 ns           11 RowInvRate=632.09ns
BM_CalcCropFirstLastInt/1000/10             149634 ns       149623 ns         4681 RowInvRate=149.623ns
BM_CalcCropFirstLastInt/10000/10           1439274 ns      1439146 ns          484 RowInvRate=143.915ns
BM_CalcCropFirstLastInt/100000/10         16519741 ns     16519427 ns           45 RowInvRate=165.194ns
BM_CalcCropFirstLastInt/1000/20             260408 ns       260361 ns         2706 RowInvRate=260.361ns
BM_CalcCropFirstLastInt/10000/20           2566381 ns      2566311 ns          274 RowInvRate=256.631ns
BM_CalcCropFirstLastInt/100000/20         30131297 ns     30130369 ns           22 RowInvRate=301.304ns
BM_CalcCropFirstLastInt/1000/40             514790 ns       514617 ns         1407 RowInvRate=514.617ns
BM_CalcCropFirstLastInt/10000/40           5224921 ns      5224683 ns          114 RowInvRate=522.468ns
BM_CalcCropFirstLastInt/100000/40         62638442 ns     62636152 ns           10 RowInvRate=626.362ns
BM_CalcCropToNullAllAllInt/1000/10          141176 ns       141121 ns         4875 RowInvRate=141.121ns
BM_CalcCropToNullAllAllInt/10000/10        1335293 ns      1335112 ns          489 RowInvRate=133.511ns
BM_CalcCropToNullAllAllInt/100000/10      13100885 ns     13100435 ns           52 RowInvRate=131.004ns
BM_CalcCropToNullAllAllInt/1000/20          250992 ns       250958 ns         2788 RowInvRate=250.958ns
BM_CalcCropToNullAllAllInt/10000/20        2476852 ns      2476640 ns          284 RowInvRate=247.664ns
BM_CalcCropToNullAllAllInt/100000/20      30356508 ns     30354874 ns           23 RowInvRate=303.549ns
BM_CalcCropToNullAllAllInt/1000/40          466767 ns       466678 ns         1506 RowInvRate=466.678ns
BM_CalcCropToNullAllAllInt/10000/40        4593207 ns      4593016 ns          146 RowInvRate=459.302ns
BM_CalcCropToNullAllAllInt/100000/40      58825231 ns     58823309 ns           12 RowInvRate=588.233ns
BM_CalcCropToNullFirstLastInt/1000/10       148399 ns       148362 ns         4712 RowInvRate=148.362ns
BM_CalcCropToNullFirstLastInt/10000/10     1455650 ns      1455562 ns          491 RowInvRate=145.556ns
BM_CalcCropToNullFirstLastInt/100000/10   14439337 ns     14438912 ns           49 RowInvRate=144.389ns
BM_CalcCropToNullFirstLastInt/1000/20       256735 ns       256711 ns         2733 RowInvRate=256.711ns
BM_CalcCropToNullFirstLastInt/10000/20     2531621 ns      2531493 ns          276 RowInvRate=253.149ns
BM_CalcCropToNullFirstLastInt/100000/20   27966533 ns     27965982 ns           26 RowInvRate=279.66ns
BM_CalcCropToNullFirstLastInt/1000/40       464120 ns       463970 ns         1508 RowInvRate=463.97ns
BM_CalcCropToNullFirstLastInt/10000/40     4567257 ns      4567089 ns          153 RowInvRate=456.709ns
BM_CalcCropToNullFirstLastInt/100000/40   51863465 ns     51862585 ns           11 RowInvRate=518.626ns
*/

// element_type is the activity element type (args 1/2/4); the begin/end range
// modes (args 3/5) are always VARCHAR. Fn is the scalar function to benchmark
// (kept as a template parameter so both calc_crop<...> and calc_crop_to_null<...>
// can be passed). Pass the calc_crop*/<element_type> that matches element_type.
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
    bench<TYPE_VARCHAR>(state, CelonisArrayFunctions::calc_crop<TYPE_VARCHAR>, "ALL", "ALL");
}

static void BM_CalcCropFirstLast(benchmark::State& state) {
    bench<TYPE_VARCHAR>(state, CelonisArrayFunctions::calc_crop<TYPE_VARCHAR>, "FIRST", "LAST");
}

static void BM_CalcCropToNullAllAll(benchmark::State& state) {
    bench<TYPE_VARCHAR>(state, CelonisArrayFunctions::calc_crop_to_null<TYPE_VARCHAR>, "ALL", "ALL");
}

static void BM_CalcCropToNullFirstLast(benchmark::State& state) {
    bench<TYPE_VARCHAR>(state, CelonisArrayFunctions::calc_crop_to_null<TYPE_VARCHAR>, "FIRST", "LAST");
}

// INT-typed variants (element type passed as the template argument to the
// scalar function).
static void BM_CalcCropAllAllInt(benchmark::State& state) {
    bench<TYPE_INT>(state, CelonisArrayFunctions::calc_crop<TYPE_INT>, "ALL", "ALL");
}

static void BM_CalcCropFirstLastInt(benchmark::State& state) {
    bench<TYPE_INT>(state, CelonisArrayFunctions::calc_crop<TYPE_INT>, "FIRST", "LAST");
}

static void BM_CalcCropToNullAllAllInt(benchmark::State& state) {
    bench<TYPE_INT>(state, CelonisArrayFunctions::calc_crop_to_null<TYPE_INT>, "ALL", "ALL");
}

static void BM_CalcCropToNullFirstLastInt(benchmark::State& state) {
    bench<TYPE_INT>(state, CelonisArrayFunctions::calc_crop_to_null<TYPE_INT>, "FIRST", "LAST");
}

// Args: Number of rows / Array Length
BENCHMARK(BM_CalcCropAllAll)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropFirstLast)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullAllAll)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullFirstLast)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropAllAllInt)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropFirstLastInt)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullAllAllInt)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

BENCHMARK(BM_CalcCropToNullFirstLastInt)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40}});

} // namespace starrocks

BENCHMARK_MAIN();
