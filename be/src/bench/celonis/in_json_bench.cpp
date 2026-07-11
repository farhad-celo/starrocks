#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/in_json.h"
#include "exprs/function_context.h"
#include "nlohmann/json.hpp"
#include "runtime/types.h"

using json = nlohmann::json;

namespace starrocks {

/*
2025-01-28T15:31:49+00:00
Running ./be/build_Release/src/bench/celonis/output/in_json_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 1.49, 2.16, 1.14
----------------------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------
BM_InJsonVARCHAR/1000/20/5         23993 ns        23999 ns        29096 RowInvRate=23.9989ns
BM_InJsonVARCHAR/10000/20/5       207027 ns       207036 ns         3379 RowInvRate=20.7036ns
BM_InJsonVARCHAR/100000/20/5     2003222 ns      2003181 ns          348 RowInvRate=20.0318ns
BM_InJsonVARCHAR/1000/40/5         21254 ns        21256 ns        32952 RowInvRate=21.2562ns
BM_InJsonVARCHAR/10000/40/5       176181 ns       176177 ns         3974 RowInvRate=17.6177ns
BM_InJsonVARCHAR/100000/40/5     1703040 ns      1703000 ns          409 RowInvRate=17.03ns
BM_InJsonVARCHAR/1000/60/5         19711 ns        19715 ns        35484 RowInvRate=19.715ns
BM_InJsonVARCHAR/10000/60/5       160857 ns       160856 ns         4348 RowInvRate=16.0856ns
BM_InJsonVARCHAR/100000/60/5     1559089 ns      1559070 ns          443 RowInvRate=15.5907ns
BM_InJsonVARCHAR/1000/20/10        28309 ns        28315 ns        24740 RowInvRate=28.3145ns
BM_InJsonVARCHAR/10000/20/10      231380 ns       231397 ns         3026 RowInvRate=23.1397ns
BM_InJsonVARCHAR/100000/20/10    2261552 ns      2261558 ns          312 RowInvRate=22.6156ns
BM_InJsonVARCHAR/1000/40/10        25115 ns        25119 ns        27813 RowInvRate=25.1194ns
BM_InJsonVARCHAR/10000/40/10      200059 ns       200059 ns         3503 RowInvRate=20.0059ns
BM_InJsonVARCHAR/100000/40/10    1935514 ns      1935493 ns          359 RowInvRate=19.3549ns
BM_InJsonVARCHAR/1000/60/10        23071 ns        23074 ns        30284 RowInvRate=23.0738ns
BM_InJsonVARCHAR/10000/60/10      180171 ns       180182 ns         3886 RowInvRate=18.0182ns
BM_InJsonVARCHAR/100000/60/10    1734872 ns      1734867 ns          405 RowInvRate=17.3487ns
*/

template <LogicalType LT>
json ToJsonArray(const DatumArray& match_array) {
    json match_array_json = json::array();
    if constexpr (lt_is_string<LT>) {
        for (const auto& item : match_array) {
            if (item.is_null()) {
                match_array_json.push_back(nullptr);
            } else {
                match_array_json.push_back(item.get_slice().to_string());
            }
        }
    } else {
        for (const auto& item : match_array) {
            if (item.is_null()) {
                match_array_json.push_back(nullptr);
            } else {
                match_array_json.push_back(item.get<RunTimeCppType<LT>>());
            }
        }
    }
    return match_array_json;
}

static void do_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_values = state.range(1);
    int match_size = state.range(2);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_values - 1);

    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    auto gen_rand_element = [&]() { return Slice(values[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                        TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BOOLEAN);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(gen_rand_element());
        }
        auto match_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto match_array = gen_rand_array(match_size);
        json match_array_json = ToJsonArray<TYPE_VARCHAR>(match_array);
        std::string match_array_json_str = match_array_json.dump();
        match_column->append_datum(Slice(match_array_json_str));
        match_column = ConstColumn::create(match_column, num_rows);
        ctx->set_constant_columns({nullptr, match_column});

        state.ResumeTiming();
        ASSERT_TRUE(CelonisInJson<TYPE_VARCHAR>::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisInJson<TYPE_VARCHAR>::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisInJson<TYPE_VARCHAR>::in_json(ctx.get(), {input_column, match_column}).ok());
        ASSERT_TRUE(CelonisInJson<TYPE_VARCHAR>::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisInJson<TYPE_VARCHAR>::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_InJsonVARCHAR(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / Number of possible values / Size of match list
BENCHMARK(BM_InJsonVARCHAR)->ArgsProduct({{1000, 10000, 100000}, {20, 40, 60}, {5, 10}});

} // namespace starrocks

BENCHMARK_MAIN();