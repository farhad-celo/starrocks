#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/celonis/multi_in.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-10-16T15:54:34+00:00
Running ./be/build_Release/src/bench/celonis/output/multi_in_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 10.28, 6.32, 2.91
Benchmark Args: Number of rows / Number of fields / Number of possible values / Match list size
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_MultiInVARCHAR/4096/2/20/5        462476 ns       462439 ns         1510 RowInvRate=112.9ns
BM_MultiInVARCHAR/10000/2/20/5      1104399 ns      1104325 ns          631 RowInvRate=110.433ns
BM_MultiInVARCHAR/100000/2/20/5    11337376 ns     11337246 ns           63 RowInvRate=113.372ns
BM_MultiInVARCHAR/4096/4/20/5        632018 ns       631955 ns         1108 RowInvRate=154.286ns
BM_MultiInVARCHAR/10000/4/20/5      1538822 ns      1538718 ns          457 RowInvRate=153.872ns
BM_MultiInVARCHAR/100000/4/20/5    15193728 ns     15193711 ns           46 RowInvRate=151.937ns
BM_MultiInVARCHAR/4096/2/80/5        457076 ns       457045 ns         1486 RowInvRate=111.583ns
BM_MultiInVARCHAR/10000/2/80/5      1101424 ns      1101311 ns          637 RowInvRate=110.131ns
BM_MultiInVARCHAR/100000/2/80/5    11327381 ns     11326712 ns           64 RowInvRate=113.267ns
BM_MultiInVARCHAR/4096/4/80/5        631581 ns       631486 ns         1095 RowInvRate=154.171ns
BM_MultiInVARCHAR/10000/4/80/5      1544173 ns      1544027 ns          458 RowInvRate=154.403ns
BM_MultiInVARCHAR/100000/4/80/5    16879126 ns     16876748 ns           46 RowInvRate=168.767ns
BM_MultiInVARCHAR/4096/2/20/10       627763 ns       627735 ns          885 RowInvRate=153.256ns
BM_MultiInVARCHAR/10000/2/20/10     1465708 ns      1465649 ns          474 RowInvRate=146.565ns
BM_MultiInVARCHAR/100000/2/20/10   16166244 ns     16164589 ns           48 RowInvRate=161.646ns
BM_MultiInVARCHAR/4096/4/20/10       809704 ns       809642 ns          866 RowInvRate=197.667ns
BM_MultiInVARCHAR/10000/4/20/10     1946672 ns      1946506 ns          361 RowInvRate=194.651ns
BM_MultiInVARCHAR/100000/4/20/10   19034298 ns     19032865 ns           37 RowInvRate=190.329ns
BM_MultiInVARCHAR/4096/2/80/10       623343 ns       623281 ns         1120 RowInvRate=152.168ns
BM_MultiInVARCHAR/10000/2/80/10     1515905 ns      1515898 ns          467 RowInvRate=151.59ns
BM_MultiInVARCHAR/100000/2/80/10   14867195 ns     14864714 ns           46 RowInvRate=148.647ns
BM_MultiInVARCHAR/4096/4/80/10       806933 ns       806871 ns          871 RowInvRate=196.99ns
BM_MultiInVARCHAR/10000/4/80/10     1916739 ns      1916588 ns          360 RowInvRate=191.659ns
BM_MultiInVARCHAR/100000/4/80/10   19186659 ns     19185272 ns           38 RowInvRate=191.853ns
*/

static void do_multi_in_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_fields = state.range(1);
    int num_values = state.range(2);
    int match_size = state.range(3);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_values - 1);

    // Generate random string values
    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    auto gen_rand_string = [&]() { return Slice(values[uniform_value(rng)]); };

    // Create function context
    // The function takes two struct arguments:
    // 1. Input struct: contains the fields to match against
    // 2. Match struct: contains arrays of potential matches (must be constant)
    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_STRUCT),
                                                        TypeDescriptor::from_logical_type(TYPE_STRUCT)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BOOLEAN);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        // Create input struct columns (the data to match against)
        Columns input_fields;
        input_fields.reserve(num_fields);
        for (int field = 0; field < num_fields; field++) {
            auto field_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
            for (int row = 0; row < num_rows; row++) {
                field_column->append_datum(gen_rand_string());
            }
            input_fields.push_back(field_column);
        }
        ColumnPtr input_struct_col = StructColumn::create(input_fields);

        // Create match struct columns (the arrays to match in - must be constant)
        Columns match_fields;
        match_fields.reserve(num_fields);
        for (int field = 0; field < num_fields; field++) {
            auto field_array_column = ColumnHelper::create_column(
                    TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);

            // Generate match array for this field
            DatumArray match_array;
            match_array.reserve(match_size);
            for (int match = 0; match < match_size; match++) {
                match_array.emplace_back(gen_rand_string());
            }
            field_array_column->append_datum(match_array);
            match_fields.push_back(field_array_column);
        }
        ColumnPtr match_struct_col = StructColumn::create(match_fields);

        ctx->set_constant_columns({nullptr, match_struct_col});
        state.ResumeTiming();

        // Execute benchmark
        ASSERT_TRUE(CelonisMultiIn::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisMultiIn::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        auto result = CelonisMultiIn::multi_in(ctx.get(), {input_struct_col, match_struct_col});
        ASSERT_TRUE(result.ok());
        ASSERT_TRUE(CelonisMultiIn::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisMultiIn::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }

    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_MultiInVARCHAR(benchmark::State& state) {
    do_multi_in_bench(state);
}

// Args: Number of rows / Number of fields / Number of possible values / Match list size
BENCHMARK(BM_MultiInVARCHAR)
        ->ArgsProduct({
                {4096, 10000, 100000}, // Number of rows
                {2, 4},                // Number of fields in struct
                {20, 80},              // Number of possible string values
                {5, 10}                // Size of match list per field
        });

} // namespace starrocks

BENCHMARK_MAIN();
