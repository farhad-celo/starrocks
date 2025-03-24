#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/binary_column.h"

namespace starrocks {

/*
2025-03-21T19:14:54+00:00
Running ./append_chunk_bench
Run on (32 X 2650 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 0.11, 0.40, 7.96
Args: Chunk size / Number of chunks / Row length / Whether to reserve before appending
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_BinaryColumnAppend/4096/100/100/0     39825817 ns     39822823 ns           18 ByteInvRate=938.273ps ByteRate=1065.79M/s ChunkInvRate=398.228us ChunkRate=2.51112k/s
BM_BinaryColumnAppend/4096/500/100/0    212468371 ns    212446953 ns            3 ByteInvRate=996.239ps ByteRate=1003.78M/s ChunkInvRate=424.894us ChunkRate=2.35353k/s
BM_BinaryColumnAppend/4096/1000/100/0   438458623 ns    438416315 ns            2 ByteInvRate=1028.31ps ByteRate=972.471M/s ChunkInvRate=438.416us ChunkRate=2.28094k/s
BM_BinaryColumnAppend/4096/2000/100/0   886697590 ns    886573819 ns            1 ByteInvRate=1043.49ps ByteRate=958.321M/s ChunkInvRate=443.287us ChunkRate=2.25588k/s
BM_BinaryColumnAppend/4096/100/1000/0   480421016 ns    480254095 ns            2 ByteInvRate=1.16839ns ByteRate=855.877M/s ChunkInvRate=4.80254ms ChunkRate=208.223/s
BM_BinaryColumnAppend/4096/500/1000/0  2241734668 ns   2241440679 ns            1 ByteInvRate=1086.12ps ByteRate=920.71M/s ChunkInvRate=4.48288ms ChunkRate=223.071/s
BM_BinaryColumnAppend/4096/1000/1000/0 4419357272 ns   4418495907 ns            1 ByteInvRate=1077.03ps ByteRate=928.478M/s ChunkInvRate=4.4185ms ChunkRate=226.321/s
BM_BinaryColumnAppend/4096/2000/1000/0 8549650358 ns   8548686636 ns            1 ByteInvRate=1039.17ps ByteRate=962.31M/s ChunkInvRate=4.27434ms ChunkRate=233.954/s
BM_BinaryColumnAppend/4096/100/100/1     18068303 ns     18059696 ns           39 ByteInvRate=424.492ps ByteRate=2.35576G/s ChunkInvRate=180.597us ChunkRate=5.53719k/s
BM_BinaryColumnAppend/4096/500/100/1     94342081 ns     94302940 ns            7 ByteInvRate=441.61ps ByteRate=2.26444G/s ChunkInvRate=188.606us ChunkRate=5.30206k/s
BM_BinaryColumnAppend/4096/1000/100/1   189305783 ns    189224795 ns            4 ByteInvRate=444.868ps ByteRate=2.24786G/s ChunkInvRate=189.225us ChunkRate=5.28472k/s
BM_BinaryColumnAppend/4096/2000/100/1   424576768 ns    424435016 ns            2 ByteInvRate=498.052ps ByteRate=2.00782G/s ChunkInvRate=212.218us ChunkRate=4.71215k/s
BM_BinaryColumnAppend/4096/100/1000/1   201893551 ns    201811607 ns            3 ByteInvRate=490.906ps ByteRate=2.03705G/s ChunkInvRate=2.01812ms ChunkRate=495.512/s
BM_BinaryColumnAppend/4096/500/1000/1  1113403663 ns   1113095230 ns            1 ByteInvRate=542.657ps ByteRate=1.84278G/s ChunkInvRate=2.22619ms ChunkRate=449.198/s
BM_BinaryColumnAppend/4096/1000/1000/1 2034549318 ns   2033936290 ns            1 ByteInvRate=495.379ps ByteRate=2.01866G/s ChunkInvRate=2.03394ms ChunkRate=491.657/s
BM_BinaryColumnAppend/4096/2000/1000/1 3912918064 ns   3911934548 ns            1 ByteInvRate=474.199ps ByteRate=2.10882G/s ChunkInvRate=1.95597ms ChunkRate=511.256/s
*/

static void do_bench(benchmark::State& state, size_t chunk_size, size_t chunk_num, size_t row_length, bool reserve) {
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());

    UniformInt uniform_length(row_length * 0.8, row_length * 1.2);
    UniformInt uniform_char('A', 'Z');

    auto rand_str = [&]() {
        std::string str;
        int length = uniform_length(rng);
        for (int i = 0; i < length; i++) {
            str += uniform_char(rng);
        }
        return str;
    };

    std::shared_ptr<BinaryColumn> column = std::make_shared<BinaryColumn>();
    for (size_t i = 0; i < chunk_size; i++) {
        column->append_string(rand_str());
    }

    size_t total_chunks = 0;
    size_t total_bytes = 0;

    for (auto _ : state) {
        BinaryColumn dest_column;
        if (reserve) {
            dest_column.reserve(chunk_size * chunk_num, chunk_size * chunk_num * row_length * 1.2);
        }
        for (int i = 0; i < chunk_num; i++) {
            dest_column.append(*column, 0, column->size());
        }
        total_chunks += chunk_num;
        total_bytes += column->byte_size() * chunk_num;
    }
    state.counters["ChunkRate"] =
            benchmark::Counter(total_chunks, benchmark::Counter::kIsRate);
    state.counters["ChunkInvRate"] =
            benchmark::Counter(total_chunks, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
    state.counters["ByteRate"] =
            benchmark::Counter(total_bytes, benchmark::Counter::kIsRate);
    state.counters["ByteInvRate"] =
            benchmark::Counter(total_bytes, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_BinaryColumnAppend(benchmark::State& state) {
    size_t chunk_size = state.range(0);
    size_t chunk_num = state.range(1);
    size_t row_length = state.range(2);
    bool reserve = state.range(3);

    do_bench(state, chunk_size, chunk_num, row_length, reserve);
}

// Args: Chunk size / Number of chunks / Row length / Whether to reserve before appending
BENCHMARK(BM_BinaryColumnAppend)->ArgsProduct({{4096}, {100, 500, 1000, 2000}, {100, 1000}, {false, true}});

} // namespace starrocks

BENCHMARK_MAIN();
