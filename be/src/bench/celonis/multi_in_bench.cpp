#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/multi_in.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-07-12T10:39:51+00:00
Running ./be/build_Release/src/bench/celonis/output/multi_in_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 7.01, 2.75, 1.86
Benchmark Args: Number of rows / Number of fields / Number of possible values / Match list size
-------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------
BM_MultiInVARCHAR/4096/2/20/5       1181214 ns      1181211 ns          595 RowInvRate=288.382ns
BM_MultiInVARCHAR/10000/2/20/5      2873957 ns      2873849 ns          244 RowInvRate=287.385ns
BM_MultiInVARCHAR/100000/2/20/5    28669293 ns     28668751 ns           24 RowInvRate=286.688ns
BM_MultiInVARCHAR/4096/4/20/5       1380452 ns      1380380 ns          507 RowInvRate=337.007ns
BM_MultiInVARCHAR/10000/4/20/5      3355092 ns      3354992 ns          209 RowInvRate=335.499ns
BM_MultiInVARCHAR/100000/4/20/5    33472649 ns     33471442 ns           21 RowInvRate=334.714ns
BM_MultiInVARCHAR/4096/2/80/5       1150478 ns      1150414 ns          607 RowInvRate=280.863ns
BM_MultiInVARCHAR/10000/2/80/5      2785650 ns      2785496 ns          251 RowInvRate=278.55ns
BM_MultiInVARCHAR/100000/2/80/5    27755278 ns     27754168 ns           25 RowInvRate=277.542ns
BM_MultiInVARCHAR/4096/4/80/5       1341091 ns      1341022 ns          522 RowInvRate=327.398ns
BM_MultiInVARCHAR/10000/4/80/5      3265264 ns      3265173 ns          215 RowInvRate=326.517ns
BM_MultiInVARCHAR/100000/4/80/5    32651062 ns     32651102 ns           22 RowInvRate=326.511ns
BM_MultiInVARCHAR/4096/2/20/10      2064097 ns      2064128 ns          339 RowInvRate=503.938ns
BM_MultiInVARCHAR/10000/2/20/10     5035397 ns      5035335 ns          138 RowInvRate=503.534ns
BM_MultiInVARCHAR/100000/2/20/10   50147647 ns     50145825 ns           14 RowInvRate=501.458ns
BM_MultiInVARCHAR/4096/4/20/10      2291597 ns      2291503 ns          306 RowInvRate=559.449ns
BM_MultiInVARCHAR/10000/4/20/10     5576950 ns      5576883 ns          126 RowInvRate=557.688ns
BM_MultiInVARCHAR/100000/4/20/10   55223732 ns     55221542 ns           13 RowInvRate=552.215ns
BM_MultiInVARCHAR/4096/2/80/10      2025429 ns      2025393 ns          346 RowInvRate=494.481ns
BM_MultiInVARCHAR/10000/2/80/10     4923023 ns      4922920 ns          142 RowInvRate=492.292ns
BM_MultiInVARCHAR/100000/2/80/10   49111270 ns     49109448 ns           14 RowInvRate=491.094ns
BM_MultiInVARCHAR/4096/4/80/10      2223478 ns      2223360 ns          315 RowInvRate=542.813ns
BM_MultiInVARCHAR/10000/4/80/10     5410736 ns      5410659 ns          129 RowInvRate=541.066ns
BM_MultiInVARCHAR/100000/4/80/10   53949881 ns     53949320 ns           13 RowInvRate=539.493ns
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
    std::vector<FunctionContext::TypeDesc> arg_types = {
        AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_STRUCT)),
        AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_STRUCT))
    };
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
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
        auto input_struct_col = StructColumn(input_fields).create(input_fields);

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
        auto match_struct_col = StructColumn(match_fields).create(match_fields);
        
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
BENCHMARK(BM_MultiInVARCHAR)->ArgsProduct({
    {4096, 10000, 100000},     // Number of rows
    {2, 4},                    // Number of fields in struct
    {20, 80},                  // Number of possible string values
    {5, 10}                    // Size of match list per field
});

} // namespace starrocks

BENCHMARK_MAIN();
