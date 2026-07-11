#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/stringhash.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

// BLAKE2
/*
2025-03-01T22:17:16+00:00
Running ./be/build_Release/src/bench/celonis/output/stringhash_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 6.84, 9.88, 6.82
// Number of rows / Average string length
-----------------------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------
BM_StringHash/1000/10        215822 ns       215835 ns         3241 RowInvRate=215.835ns
BM_StringHash/10000/10      2157227 ns      2157192 ns          325 RowInvRate=215.719ns
BM_StringHash/100000/10    21497957 ns     21497277 ns           32 RowInvRate=214.973ns
BM_StringHash/1000/20        215048 ns       215047 ns         3254 RowInvRate=215.047ns
BM_StringHash/10000/20      2147054 ns      2147035 ns          322 RowInvRate=214.704ns
BM_StringHash/100000/20    21394303 ns     21393045 ns           33 RowInvRate=213.93ns
BM_StringHash/1000/40        215658 ns       215654 ns         3242 RowInvRate=215.654ns
BM_StringHash/10000/40      2153857 ns      2153803 ns          325 RowInvRate=215.38ns
BM_StringHash/100000/40    21695925 ns     21695679 ns           33 RowInvRate=216.957ns
BM_StringHash/1000/80        330061 ns       330044 ns         2121 RowInvRate=330.044ns
BM_StringHash/10000/80      3296920 ns      3296667 ns          213 RowInvRate=329.667ns
BM_StringHash/100000/80    32945756 ns     32942979 ns           21 RowInvRate=329.43ns
BM_StringHash/1000/160       481645 ns       481614 ns         1447 RowInvRate=481.614ns
BM_StringHash/10000/160     4802145 ns      4801827 ns          146 RowInvRate=480.183ns
BM_StringHash/100000/160   48091774 ns     48089409 ns           15 RowInvRate=480.894ns
*/

// OpenSSL
/*
2026-04-24T23:44:39+00:00
Running ./be/build_Release/src/bench/celonis/output/stringhash_bench
Run on (32 X 3243.36 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.67, 2.14, 1.56
// Number of rows / Average string length
-----------------------------------------------------------------------------------
Benchmark                         Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------
BM_StringHash/1000/10        244938 ns       244950 ns         2858 RowInvRate=244.95ns
BM_StringHash/10000/10      2448799 ns      2448707 ns          286 RowInvRate=244.871ns
BM_StringHash/100000/10    24407568 ns     24407221 ns           29 RowInvRate=244.072ns
BM_StringHash/1000/20        245367 ns       245365 ns         2852 RowInvRate=245.365ns
BM_StringHash/10000/20      2455035 ns      2454899 ns          285 RowInvRate=245.49ns
BM_StringHash/100000/20    24581069 ns     24579992 ns           29 RowInvRate=245.8ns
BM_StringHash/1000/40        248266 ns       248253 ns         2819 RowInvRate=248.253ns
BM_StringHash/10000/40      2479812 ns      2479705 ns          282 RowInvRate=247.97ns
BM_StringHash/100000/40    24736413 ns     24736279 ns           28 RowInvRate=247.363ns
BM_StringHash/1000/80        369964 ns       369935 ns         1893 RowInvRate=369.935ns
BM_StringHash/10000/80      3684807 ns      3684609 ns          190 RowInvRate=368.461ns
BM_StringHash/100000/80    36774202 ns     36772388 ns           19 RowInvRate=367.724ns
BM_StringHash/1000/160       522055 ns       521989 ns         1338 RowInvRate=521.989ns
BM_StringHash/10000/160     5203765 ns      5203548 ns          135 RowInvRate=520.355ns
BM_StringHash/100000/160   52023684 ns     52020526 ns           13 RowInvRate=520.205ns
*/

class RandomStringGenerator {
private:
    std::mt19937 generator;
    const std::string char_set;
    std::uniform_int_distribution<int> char_distribution;

public:
    RandomStringGenerator()
            : generator(std::random_device()()), // Seed the generator once
              char_set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"),
              char_distribution(0, char_set.size() - 1) {}

    RandomStringGenerator(const std::string& custom_char_set)
            : generator(std::random_device()()),
              char_set(custom_char_set),
              char_distribution(0, custom_char_set.size() - 1) {}

    std::string generate(int average_length) {
        // Use normal distribution to determine the actual length
        std::normal_distribution<double> length_distribution(average_length, average_length * 0.2);
        int actual_length = std::max(1, static_cast<int>(std::round(length_distribution(generator))));
        // Generate the random string
        std::string random_string;
        random_string.reserve(actual_length);
        for (int i = 0; i < actual_length; ++i) {
            random_string += char_set[char_distribution(generator)];
        }
        return random_string;
    }
};

static void BM_StringHash(benchmark::State& state) {
    int num_rows = state.range(0);
    int avg_length = state.range(1);
    RandomStringGenerator gen;

    std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_VARCHAR);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        for (int i = 0; i < num_rows; i++) {
            input_column->append_datum(Slice(gen.generate(avg_length)));
        }
        ctx->set_constant_columns({nullptr});
        state.ResumeTiming();
        EXPECT_TRUE(CelonisStringhash::stringhash(ctx.get(), {input_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows / Average string length
BENCHMARK(BM_StringHash)->ArgsProduct({{1000, 10000, 100000}, {10, 20, 40, 80, 160}});

} // namespace starrocks

BENCHMARK_MAIN();
