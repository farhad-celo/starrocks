#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/abc_model.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

/*
2025-06-18T11:21:30+00:00
Running ./be/build_Release/src/bench/celonis/output/apply_abc_model_bench
Run on (32 X 3188.11 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.10, 16.94, 11.69
// Args: Number of rows / Number of special cases in model
---------------------------------------------------------------------------------------------
Benchmark                                   Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------
BM_AbcModelConstantBigInt/1000/0        19409 ns        19418 ns        36185 RowsPerSecond=51.4979M/s TimePerRow=19.4183ns
BM_AbcModelConstantBigInt/10000/0      171889 ns       171811 ns         4082 RowsPerSecond=58.2036M/s TimePerRow=17.1811ns
BM_AbcModelConstantBigInt/100000/0    1684880 ns      1684906 ns          416 RowsPerSecond=59.3505M/s TimePerRow=16.8491ns
BM_AbcModelConstantBigInt/1000/1        22307 ns        22315 ns        31003 RowsPerSecond=44.8127M/s TimePerRow=22.3151ns
BM_AbcModelConstantBigInt/10000/1      177448 ns       177382 ns         3946 RowsPerSecond=56.3754M/s TimePerRow=17.7382ns
BM_AbcModelConstantBigInt/100000/1    1726741 ns      1726574 ns          408 RowsPerSecond=57.9182M/s TimePerRow=17.2657ns
BM_AbcModelConstantBigInt/1000/2        23856 ns        23867 ns        27742 RowsPerSecond=41.8986M/s TimePerRow=23.8671ns
BM_AbcModelConstantBigInt/10000/2      179068 ns       179007 ns         3908 RowsPerSecond=55.8637M/s TimePerRow=17.9007ns
BM_AbcModelConstantBigInt/100000/2    1708049 ns      1708040 ns          410 RowsPerSecond=58.5466M/s TimePerRow=17.0804ns
BM_AbcModelConstantDouble/1000/0        26201 ns        26209 ns        26845 RowsPerSecond=38.155M/s TimePerRow=26.2089ns
BM_AbcModelConstantDouble/10000/0      222342 ns       222275 ns         3151 RowsPerSecond=44.9893M/s TimePerRow=22.2275ns
BM_AbcModelConstantDouble/100000/0    2218220 ns      2218188 ns          323 RowsPerSecond=45.0818M/s TimePerRow=22.1819ns
BM_AbcModelConstantDouble/1000/1        29956 ns        29966 ns        24114 RowsPerSecond=33.3713M/s TimePerRow=29.9659ns
BM_AbcModelConstantDouble/10000/1      231675 ns       231602 ns         2852 RowsPerSecond=43.1775M/s TimePerRow=23.1602ns
BM_AbcModelConstantDouble/100000/1    2562284 ns      2552380 ns          309 RowsPerSecond=39.1791M/s TimePerRow=25.5238ns
BM_AbcModelConstantDouble/1000/2        31315 ns        31330 ns        22241 RowsPerSecond=31.9179M/s TimePerRow=31.3304ns
BM_AbcModelConstantDouble/10000/2      243325 ns       243262 ns         2946 RowsPerSecond=41.108M/s TimePerRow=24.3262ns
BM_AbcModelConstantDouble/100000/2    2366804 ns      2366738 ns          278 RowsPerSecond=42.2522M/s TimePerRow=23.6674ns
*/

namespace starrocks {

template<LogicalType LT>
static void do_bench_abc_model(benchmark::State& state) {
    using CppType = RunTimeCppType<LT>;

    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<int64_t> pk_hash_dist(
            std::numeric_limits<int64_t>::min(),
            std::numeric_limits<int64_t>::max()
    );

    int num_rows = state.range(0);
    int num_special_cases = state.range(1);  // Number of values with probability distributions

    // Generate model string
    std::string model_str;
    if constexpr (LT == TYPE_BIGINT) {
        // Example ranges for BIGINT: [0,100], [200,300], [400,500]
        model_str = "0,100,200,300,400,500:";

        // Add special cases with probability distributions
        for (int i = 0; i < num_special_cases; i++) {
            if (i > 0) model_str += ";";
            int64_t value = 50 + i * 5;  // Values within first range
            model_str += std::to_string(value) + ",0.3,0.4,0.3";
        }
    } else {  // TYPE_DOUBLE
        // Example ranges for DOUBLE: [0.0,100.0], [200.0,300.0], [400.0,500.0]
        model_str = "0.0,100.0,200.0,300.0,400.0,500.0:";

        // Add special cases with probability distributions
        for (int i = 0; i < num_special_cases; i++) {
            if (i > 0) model_str += ";";
            double value = 50.0 + i * 5.0;  // Values within first range
            model_str += std::to_string(value) + ",0.3,0.4,0.3";
        }
    }

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(LT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))
    };
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    // Generate value distribution
    std::uniform_real_distribution<double> range_selector(0.0, 1.0);
    auto generate_value = [&]() -> CppType {
        double r = range_selector(rng);
        if constexpr (LT == TYPE_BIGINT) {
            if (r < 0.4) {  // 40% in range 1
                return std::uniform_int_distribution<int64_t>(0, 100)(rng);
            } else if (r < 0.7) {  // 30% in range 2
                return std::uniform_int_distribution<int64_t>(200, 300)(rng);
            } else if (r < 0.9) {  // 20% in range 3
                return std::uniform_int_distribution<int64_t>(400, 500)(rng);
            } else {  // 10% out of range
                return std::uniform_int_distribution<int64_t>(600, 700)(rng);
            }
        } else {  // TYPE_DOUBLE
            if (r < 0.4) {  // 40% in range 1
                return std::uniform_real_distribution<double>(0.0, 100.0)(rng);
            } else if (r < 0.7) {  // 30% in range 2
                return std::uniform_real_distribution<double>(200.0, 300.0)(rng);
            } else if (r < 0.9) {  // 20% in range 3
                return std::uniform_real_distribution<double>(400.0, 500.0)(rng);
            } else {  // 10% out of range
                return std::uniform_real_distribution<double>(600.0, 700.0)(rng);
            }
        }
    };

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;

        // Create columns
        ColumnPtr value_column = ColumnHelper::create_column(TypeDescriptor(LT), true);
        ColumnPtr pk_hash_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);

        // Fill data
        for (int i = 0; i < num_rows; i++) {
            value_column->append_datum(generate_value());
            pk_hash_column->append_datum(pk_hash_dist(rng));
        }

        // Model column is constant
        ColumnPtr model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(
                Slice(model_str), num_rows);

        Columns columns;
        columns.push_back(value_column);
        columns.push_back(pk_hash_column);
        columns.push_back(model_column);
        ctx->set_constant_columns(columns);

        state.ResumeTiming();
        ASSERT_OK(CelonisAbcModel<LT>::prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        auto result = CelonisAbcModel<LT>::apply_abc_model(ctx.get(), columns);
        EXPECT_TRUE(result.ok());
        EXPECT_FALSE(result.value().get()->only_null());
        ASSERT_OK(CelonisAbcModel<LT>::close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    }

    state.counters["RowsPerSecond"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate);
    state.counters["TimePerRow"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_AbcModelConstantBigInt(benchmark::State& state) {
    do_bench_abc_model<TYPE_BIGINT>(state);
}

static void BM_AbcModelConstantDouble(benchmark::State& state) {
    do_bench_abc_model<TYPE_DOUBLE>(state);
}

// Args: Number of rows / Number of special cases in model
BENCHMARK(BM_AbcModelConstantBigInt)->ArgsProduct({{1000, 10000, 100000}, {0, 1, 2}});
BENCHMARK(BM_AbcModelConstantDouble)->ArgsProduct({{1000, 10000, 100000}, {0, 1, 2}});

} // namespace starrocks

BENCHMARK_MAIN();