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
2026-09-10T20:49:15+00:00                                                                                                                                                                                        
Running ./be/build_Release/src/bench/celonis/output/array_functions_bench                                                                                                                                        
Run on (32 X 3111.34 MHz CPU s)                                                                                                                                                                                  
CPU Caches:                                                                                                                                                                                                      
  L1 Data 32 KiB (x16)                                                                                                                                                                                           
  L1 Instruction 32 KiB (x16)                                                                                                                                                                                    
  L2 Unified 1024 KiB (x16)                                                                                                                                                                                      
  L3 Unified 33792 KiB (x1)                                                                                                                                                                                      
Load Average: 1.00, 5.44, 14.11                                                                                                                                                                                  
----------------------------------------------------------------------------------------------                                                                                                                   
Benchmark                                    Time             CPU   Iterations UserCounters...                                                                                                                   
----------------------------------------------------------------------------------------------                                                                                                                   
BM_DedupSortedByVARCHAR/10000/10/5     4438195 ns      4437790 ns          175 RowInvRate=443.779ns                                                                                                              
BM_DedupSortedByVARCHAR/10000/20/5    14535057 ns     14533813 ns           48 RowInvRate=1.45338us
BM_DedupSortedByVARCHAR/10000/10/10   11008668 ns     11007665 ns           63 RowInvRate=1.10077us
BM_DedupSortedByVARCHAR/10000/20/10   24666441 ns     24666067 ns           28 RowInvRate=2.46661us
BM_DedupSortedByINT/10000/10/5         1645008 ns      1644450 ns          426 RowInvRate=164.445ns
BM_DedupSortedByINT/10000/20/5         4279992 ns      4279077 ns          160 RowInvRate=427.908ns
BM_DedupSortedByINT/10000/10/10        2127412 ns      2126792 ns          323 RowInvRate=212.679ns
BM_DedupSortedByINT/10000/20/10        6188531 ns      6187523 ns          113 RowInvRate=618.752ns
BM_ArrayBoolOr/1000/1/10                 12674 ns        12578 ns        55708 RowInvRate=12.5784ns
BM_ArrayBoolOr/10000/1/10               111571 ns       111358 ns         6299 RowInvRate=11.1358ns
BM_ArrayBoolOr/1000/10/10                18342 ns        18264 ns        38367 RowInvRate=18.2635ns
BM_ArrayBoolOr/10000/10/10              167723 ns       167531 ns         4194 RowInvRate=16.7531ns
BM_ArrayBoolOr/1000/50/10                18229 ns        18140 ns        38770 RowInvRate=18.1404ns
BM_ArrayBoolOr/10000/50/10              166351 ns       166152 ns         4221 RowInvRate=16.6152ns
BM_ArrayBoolOr/1000/1/20                 17476 ns        17362 ns        40344 RowInvRate=17.3622ns
BM_ArrayBoolOr/10000/1/20               157288 ns       156942 ns         4471 RowInvRate=15.6942ns
BM_ArrayBoolOr/1000/10/20                23079 ns        22976 ns        30458 RowInvRate=22.9763ns
BM_ArrayBoolOr/10000/10/20              213811 ns       213423 ns         3286 RowInvRate=21.3423ns
BM_ArrayBoolOr/1000/50/20                18312 ns        18194 ns        38486 RowInvRate=18.1939ns
BM_ArrayBoolOr/10000/50/20              166294 ns       165975 ns         4218 RowInvRate=16.5975ns
BM_ArrayCountVarchar/1000/0/20            9628 ns         9512 ns        73619 RowInvRate=9.51246ns
BM_ArrayCountVarchar/10000/0/20          75559 ns        74925 ns         9388 RowInvRate=7.49246ns
BM_ArrayCountVarchar/1000/10/20          17491 ns        17343 ns        40128 RowInvRate=17.3427ns
BM_ArrayCountVarchar/10000/10/20        147212 ns       146544 ns         4793 RowInvRate=14.6544ns
BM_ArrayCountVarchar/1000/50/20          18118 ns        17949 ns        39509 RowInvRate=17.949ns
BM_ArrayCountVarchar/10000/50/20        146871 ns       146238 ns         4670 RowInvRate=14.6238ns
BM_ArrayCountVarchar/1000/0/100          11725 ns        11294 ns        61840 RowInvRate=11.2935ns
BM_ArrayCountVarchar/10000/0/100        458850 ns       457389 ns         1561 RowInvRate=45.7389ns
BM_ArrayCountVarchar/1000/10/100         35099 ns        34645 ns        20126 RowInvRate=34.6451ns
BM_ArrayCountVarchar/10000/10/100       651415 ns       650497 ns         1062 RowInvRate=65.0497ns
BM_ArrayCountVarchar/1000/50/100         35298 ns        34784 ns        20032 RowInvRate=34.7841ns
BM_ArrayCountVarchar/10000/50/100       322519 ns       321275 ns         2175 RowInvRate=32.1275ns
*/

// KeyType is the element type of key_array: TYPE_VARCHAR for the decoded strings and TYPE_INT for the global
// dictionary codes the dictified plan passes instead.
template <LogicalType KeyType>
static void BM_DedupSortedBy(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_unique_elements = state.range(1);
    int max_duplicates = state.range(2);

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    int num_values = num_unique_elements * 10; // Some number. This doesn't matter much unless it is too small.
    UniformInt uniform_value(0, num_values - 1);
    UniformInt uniform_duplicates(1, max_duplicates);

    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    const auto input_array_type = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR));
    const auto key_array_type = TypeDescriptor::create_array_type(TypeDescriptor(KeyType));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column = ColumnHelper::create_column(input_array_type, true);
        auto key_column = ColumnHelper::create_column(key_array_type, true);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            DatumArray key_array;
            for (int j = 0; j < num_unique_elements; j++) {
                Slice input = values[uniform_value(rng)];
                // To be strict, we need to generate a key different from the previous one. But it wouldn't matter much.
                int32_t key_index = uniform_value(rng);
                Datum key;
                if constexpr (KeyType == TYPE_VARCHAR) {
                    key = Datum(Slice(values[key_index]));
                } else {
                    key = Datum(key_index);
                }
                int num_duplicates = uniform_duplicates(rng);
                for (int k = 0; k < num_duplicates; k++) {
                    input_array.emplace_back(input);
                    key_array.emplace_back(key);
                }
            }
            input_column->append_datum(input_array);
            key_column->append_datum(key_array);
        }

        state.ResumeTiming();
        auto result = CelonisDedupSortedBy<KeyType>::process({input_column, key_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_DedupSortedByVARCHAR(benchmark::State& state) {
    BM_DedupSortedBy<TYPE_VARCHAR>(state);
}

static void BM_DedupSortedByINT(benchmark::State& state) {
    BM_DedupSortedBy<TYPE_INT>(state);
}

static void BM_ArrayBoolOr(benchmark::State& state) {
    int num_rows = state.range(0);
    double probability = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BOOLEAN))};
    auto return_type = TypeDescriptor(TYPE_BOOLEAN);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(probability);

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BOOLEAN)), true);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            for (int j = 0; j < array_length; j++) {
                input_array.emplace_back(dist(gen));
            }
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = CelonisArrayFunctions::array_bool_or(ctx.get(), {input_column});

        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ArrayCountVarchar(benchmark::State& state) {
    int num_rows = state.range(0);
    double null_probability = state.range(1) / 100.0;
    int array_length = state.range(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR))};
    auto return_type = TypeDescriptor(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution dist(null_probability);

    std::vector<std::string> strings;
    strings.reserve(array_length);
    for (int j = 0; j < array_length; ++j) {
        strings.emplace_back("value" + std::to_string(j));
    }
    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto input_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), true);
        for (int i = 0; i < num_rows; i++) {
            DatumArray input_array;
            for (int j = 0; j < array_length; j++) {
                bool is_null = dist(gen);
                if (is_null) {
                    input_array.emplace_back(kNullDatum);
                } else {
                    input_array.emplace_back(Slice(strings[j]));
                }
            }
            input_column->append_datum(input_array);
        }

        state.ResumeTiming();
        auto result = CelonisArrayFunctions::array_count(ctx.get(), {input_column});

        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of unique elements in the array / Max number of duplicates per unique element
BENCHMARK(BM_DedupSortedByVARCHAR)->ArgsProduct({{10000}, {10, 20}, {5, 10}});
BENCHMARK(BM_DedupSortedByINT)->ArgsProduct({{10000}, {10, 20}, {5, 10}});

// Args: Number of rows / True percentage / Array length
BENCHMARK(BM_ArrayBoolOr)->ArgsProduct({{1000, 10000}, {1, 10, 50}, {10, 20}});

// Args: Number of rows / Null percentage / Array length
BENCHMARK(BM_ArrayCountVarchar)->ArgsProduct({{1000, 10000}, {0, 10, 50}, {20, 100}});

} // namespace starrocks

BENCHMARK_MAIN();
