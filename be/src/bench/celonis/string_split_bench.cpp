#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-04-04T12:16:20+00:00
Running ./be/build_Release/src/bench/celonis/output/string_split_bench
Run on (32 X 3202.55 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 4.33, 2.54, 1.47
// Args: Number of rows / String length / Pattern length / Split index / Split groups
-----------------------------------------------------------------------------------------
Benchmark                               Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------
BM_StringSplit/1000/20/0/1/2        17099 ns        17083 ns        40856 RowInvRate=17.0828ns
BM_StringSplit/10000/20/0/1/2      160269 ns       160228 ns         4386 RowInvRate=16.0228ns
BM_StringSplit/100000/20/0/1/2    1566621 ns      1566386 ns          443 RowInvRate=15.6639ns
BM_StringSplit/1000/40/0/1/2        17301 ns        17279 ns        40467 RowInvRate=17.279ns
BM_StringSplit/10000/40/0/1/2      159632 ns       159607 ns         4371 RowInvRate=15.9607ns
BM_StringSplit/100000/40/0/1/2    1565119 ns      1564965 ns          446 RowInvRate=15.6496ns
BM_StringSplit/1000/20/2/1/2        47433 ns        47424 ns        14716 RowInvRate=47.4244ns
BM_StringSplit/10000/20/2/1/2      459969 ns       459967 ns         1526 RowInvRate=45.9967ns
BM_StringSplit/100000/20/2/1/2    4543955 ns      4543864 ns          154 RowInvRate=45.4386ns
BM_StringSplit/1000/40/2/1/2        55946 ns        55935 ns        12498 RowInvRate=55.9346ns
BM_StringSplit/10000/40/2/1/2      546019 ns       545993 ns         1290 RowInvRate=54.5993ns
BM_StringSplit/100000/40/2/1/2    5362530 ns      5362148 ns          130 RowInvRate=53.6215ns
BM_StringSplit/1000/20/0/2/2        18475 ns        18458 ns        37877 RowInvRate=18.4578ns
BM_StringSplit/10000/20/0/2/2      171826 ns       171805 ns         4044 RowInvRate=17.1805ns
BM_StringSplit/100000/20/0/2/2    1691925 ns      1691810 ns          411 RowInvRate=16.9181ns
BM_StringSplit/1000/40/0/2/2        18684 ns        18665 ns        37356 RowInvRate=18.6648ns
BM_StringSplit/10000/40/0/2/2      171790 ns       171758 ns         4085 RowInvRate=17.1758ns
BM_StringSplit/100000/40/0/2/2    1688401 ns      1688268 ns          418 RowInvRate=16.8827ns
BM_StringSplit/1000/20/2/2/2        43824 ns        43803 ns        15936 RowInvRate=43.8033ns
BM_StringSplit/10000/20/2/2/2      426529 ns       426499 ns         1638 RowInvRate=42.6499ns
BM_StringSplit/100000/20/2/2/2    4237675 ns      4237582 ns          166 RowInvRate=42.3758ns
BM_StringSplit/1000/40/2/2/2        50451 ns        50428 ns        14070 RowInvRate=50.4282ns
BM_StringSplit/10000/40/2/2/2      485995 ns       485960 ns         1449 RowInvRate=48.596ns
BM_StringSplit/100000/40/2/2/2    4805871 ns      4805590 ns          146 RowInvRate=48.0559ns
BM_StringSplit/1000/20/0/1/4        17372 ns        17350 ns        40298 RowInvRate=17.3503ns
BM_StringSplit/10000/20/0/1/4      159681 ns       159666 ns         4375 RowInvRate=15.9666ns
BM_StringSplit/100000/20/0/1/4    1581296 ns      1581122 ns          443 RowInvRate=15.8112ns
BM_StringSplit/1000/40/0/1/4        17402 ns        17380 ns        40264 RowInvRate=17.3804ns
BM_StringSplit/10000/40/0/1/4      159499 ns       159474 ns         4389 RowInvRate=15.9474ns
BM_StringSplit/100000/40/0/1/4    1563366 ns      1563213 ns          450 RowInvRate=15.6321ns
BM_StringSplit/1000/20/2/1/4        40182 ns        40164 ns        17456 RowInvRate=40.1641ns
BM_StringSplit/10000/20/2/1/4      387506 ns       387492 ns         1806 RowInvRate=38.7492ns
BM_StringSplit/100000/20/2/1/4    3817708 ns      3817650 ns          183 RowInvRate=38.1765ns
BM_StringSplit/1000/40/2/1/4        48017 ns        48001 ns        14511 RowInvRate=48.0011ns
BM_StringSplit/10000/40/2/1/4      465612 ns       465587 ns         1496 RowInvRate=46.5587ns
BM_StringSplit/100000/40/2/1/4    4631302 ns      4631021 ns          152 RowInvRate=46.3102ns
BM_StringSplit/1000/20/0/2/4        18324 ns        18304 ns        38300 RowInvRate=18.3045ns
BM_StringSplit/10000/20/0/2/4      171403 ns       171398 ns         4048 RowInvRate=17.1398ns
BM_StringSplit/100000/20/0/2/4    1688496 ns      1688343 ns          416 RowInvRate=16.8834ns
BM_StringSplit/1000/40/0/2/4        18547 ns        18525 ns        37774 RowInvRate=18.5246ns
BM_StringSplit/10000/40/0/2/4      171547 ns       171522 ns         4094 RowInvRate=17.1522ns
BM_StringSplit/100000/40/0/2/4    1679031 ns      1678900 ns          418 RowInvRate=16.789ns
BM_StringSplit/1000/20/2/2/4        52262 ns        52246 ns        13469 RowInvRate=52.246ns
BM_StringSplit/10000/20/2/2/4      507005 ns       506986 ns         1374 RowInvRate=50.6986ns
BM_StringSplit/100000/20/2/2/4    5041173 ns      5040945 ns          139 RowInvRate=50.4095ns
BM_StringSplit/1000/40/2/2/4        65222 ns        65206 ns        10742 RowInvRate=65.2056ns
BM_StringSplit/10000/40/2/2/4      633360 ns       633318 ns         1103 RowInvRate=63.3318ns
BM_StringSplit/100000/40/2/2/4    6247182 ns      6247021 ns          111 RowInvRate=62.4702ns
*/

std::string gen_random_str(int min_length, int max_length) {
    DCHECK_GE(max_length, min_length);
    if (min_length == 0) {
        return "";
    }
    static std::string alphanum =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> length_dist(min_length, max_length);
    std::uniform_int_distribution<int> char_dist(0, alphanum.size() - 1);

    int str_len = length_dist(gen);
    std::string result;
    result.reserve(str_len);
    for (int i = 0; i < str_len; i++) {
        result += alphanum[char_dist(gen)];
    }
    return result;
}

static void do_bench(benchmark::State& state) {
    int num_rows = state.range(0);
    int str_length = state.range(1);
    int pattern_length = state.range(2);
    int64_t split_index = state.range(3);
    int num_groups = state.range(4);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    // Use delimiters from the suggested characters
    static std::string delimiter_chars = ",;.?&|@#$";
    static std::mt19937 pattern_gen(std::random_device{}());
    std::uniform_int_distribution<int> char_dist(0, delimiter_chars.size() - 1);

    int total_rows = 0;
    for (auto _: state) {
        state.PauseTiming();
        total_rows += num_rows;

        std::string pattern;
        pattern.reserve(pattern_length);
        for (int i = 0; i < pattern_length; i++) {
            pattern += delimiter_chars[char_dist(pattern_gen)];
        }

        ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr pattern_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        ColumnPtr split_index_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);

        pattern_column->append_datum(pattern.data());
        split_index_column->append_datum(split_index);
        pattern_column = ConstColumn::create(pattern_column, num_rows);
        split_index_column = ConstColumn::create(split_index_column, num_rows);

        // Calculate average length for each group to maintain target string length
        int avg_group_length = (str_length - (num_groups - 1) * pattern_length) / num_groups;
        if (avg_group_length < 1) {
            avg_group_length = 1;
        }

        // Generate input strings with the desired number of split groups
        for (int i = 0; i < num_rows; i++) {
            std::string input_str;

            for (int j = 0; j < num_groups; j++) {
                // Add a random string for this group
                input_str += gen_random_str(1, avg_group_length);
                if (j < num_groups - 1) {
                    input_str += pattern;
                }
            }

            input_column->append_datum(input_str.data());
        }

        ctx->set_constant_columns({nullptr, pattern_column, split_index_column});
        Columns columns;
        columns.push_back(input_column);
        columns.push_back(pattern_column);
        columns.push_back(split_index_column);
        state.ResumeTiming();
        EXPECT_TRUE(CelonisStringFunctions::string_split(ctx.get(), columns).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_StringSplit(benchmark::State& state) {
    do_bench(state);
}

// Args: Number of rows / String length / Pattern length / Split index / Split groups
BENCHMARK(BM_StringSplit)->ArgsProduct({{1000, 10000, 100000}, {20, 40}, {0, 2}, {1, 2}, {2, 4}});

} // namespace starrocks

BENCHMARK_MAIN();