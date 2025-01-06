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
2025-01-06T15:58:39+00:00
Running ./be/build_Release/src/bench/celonis/output/calc_crop_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 13.40, 40.71, 22.48
// Args: Number of rows / Array Length
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_CalcCropAllAll/1000/10                508778 ns       508810 ns         1376 RowInvRate=508.81ns
BM_CalcCropAllAll/10000/10              4987219 ns      4987223 ns          139 RowInvRate=498.722ns
BM_CalcCropAllAll/100000/10            52574761 ns     52573787 ns           14 RowInvRate=525.738ns
BM_CalcCropAllAll/1000/20                827057 ns       827020 ns          845 RowInvRate=827.02ns
BM_CalcCropAllAll/10000/20              8125734 ns      8125284 ns           86 RowInvRate=812.528ns
BM_CalcCropAllAll/100000/20            86397121 ns     86392861 ns            8 RowInvRate=863.929ns
BM_CalcCropAllAll/1000/40               1398227 ns      1398128 ns          498 RowInvRate=1.39813us
BM_CalcCropAllAll/10000/40             14054335 ns     14054116 ns           50 RowInvRate=1.40541us
BM_CalcCropAllAll/100000/40           151190300 ns    151185622 ns            5 RowInvRate=1.51186us

BM_CalcCropFirstLast/1000/10             651705 ns       651700 ns         1066 RowInvRate=651.7ns
BM_CalcCropFirstLast/10000/10           6360570 ns      6360464 ns          111 RowInvRate=636.046ns
BM_CalcCropFirstLast/100000/10         66912351 ns     66912275 ns           11 RowInvRate=669.123ns
BM_CalcCropFirstLast/1000/20            1096535 ns      1096512 ns          641 RowInvRate=1096.51ns
BM_CalcCropFirstLast/10000/20          10828793 ns     10828567 ns           64 RowInvRate=1082.86ns
BM_CalcCropFirstLast/100000/20        107515055 ns    107511899 ns            6 RowInvRate=1075.12ns
BM_CalcCropFirstLast/1000/40            2097479 ns      2097474 ns          328 RowInvRate=2.09747us
BM_CalcCropFirstLast/10000/40          21044959 ns     21044676 ns           34 RowInvRate=2.10447us
BM_CalcCropFirstLast/100000/40        208208792 ns    208195942 ns            3 RowInvRate=2.08196us

BM_CalcCropToNullAllAll/1000/10          622573 ns       622590 ns         1125 RowInvRate=622.59ns
BM_CalcCropToNullAllAll/10000/10        6156328 ns      6155724 ns          110 RowInvRate=615.572ns
BM_CalcCropToNullAllAll/100000/10      64919210 ns     64919455 ns           11 RowInvRate=649.195ns
BM_CalcCropToNullAllAll/1000/20         1002804 ns      1002728 ns          703 RowInvRate=1002.73ns
BM_CalcCropToNullAllAll/10000/20        9918344 ns      9918375 ns           70 RowInvRate=991.838ns
BM_CalcCropToNullAllAll/100000/20     110146323 ns    110135004 ns            6 RowInvRate=1.10135us
BM_CalcCropToNullAllAll/1000/40         1777793 ns      1777710 ns          394 RowInvRate=1.77771us
BM_CalcCropToNullAllAll/10000/40       17675059 ns     17674154 ns           40 RowInvRate=1.76742us
BM_CalcCropToNullAllAll/100000/40     197847362 ns    197843700 ns            4 RowInvRate=1.97844us

BM_CalcCropToNullFirstLast/1000/10       713351 ns       713315 ns          980 RowInvRate=713.315ns
BM_CalcCropToNullFirstLast/10000/10     7029752 ns      7029181 ns          100 RowInvRate=702.918ns
BM_CalcCropToNullFirstLast/100000/10   71834192 ns     71829250 ns           10 RowInvRate=718.292ns
BM_CalcCropToNullFirstLast/1000/20      1193485 ns      1193241 ns          586 RowInvRate=1.19324us
BM_CalcCropToNullFirstLast/10000/20    11672997 ns     11672601 ns           60 RowInvRate=1.16726us
BM_CalcCropToNullFirstLast/100000/20  119040800 ns    119033601 ns            6 RowInvRate=1.19034us
BM_CalcCropToNullFirstLast/1000/40      2273841 ns      2273722 ns          308 RowInvRate=2.27372us
BM_CalcCropToNullFirstLast/10000/40    22268363 ns     22267448 ns           31 RowInvRate=2.22674us
BM_CalcCropToNullFirstLast/100000/40  216203200 ns    216198616 ns            3 RowInvRate=2.16199us
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
