#include <benchmark/benchmark.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "bench/celonis/bench_graph_generator.h"
#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/type_traits.h"
#include "common/config.h"
#include "exprs/celonis/table_function/object_link_extract_paths.h"
#include "runtime/current_thread.h"
#include "runtime/exec_env.h"
#include "runtime/runtime_state.h"
#include "util/mem_info.h"

namespace starrocks {

/*
2026-09-16T06:16:50+00:00
Running ./be/build_Release/src/bench/celonis/output/object_link_extract_paths_bench
Run on (16 X 4900 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 1280 KiB (x8)
  L3 Unified 24576 KiB (x1)
Load Average: 8.73, 4.79, 2.80
---------------------------------------------------------------------------------------------------------------
Benchmark                                                     Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------------------
BM_ExtractPaths_StandardProcessDAG/100000/10              0.037 ms        0.037 ms        19435 ChunksProcessed=1 OutputPaths=88
BM_ExtractPaths_StandardProcessDAG/1000000/10             0.045 ms        0.045 ms        15209 ChunksProcessed=1 OutputPaths=88
BM_ExtractPaths_StandardProcessDAG/10000000/10            0.173 ms        0.172 ms         4100 ChunksProcessed=1 OutputPaths=88
BM_ExtractPaths_StandardProcessDAG/100000/15              0.964 ms        0.964 ms          735 ChunksProcessed=1 OutputPaths=986
BM_ExtractPaths_StandardProcessDAG/1000000/15             0.967 ms        0.967 ms          754 ChunksProcessed=1 OutputPaths=986
BM_ExtractPaths_StandardProcessDAG/10000000/15             1.11 ms         1.11 ms          559 ChunksProcessed=1 OutputPaths=986
BM_ExtractPaths_StandardProcessDAG/100000/20               30.8 ms         30.8 ms           23 ChunksProcessed=3 OutputPaths=10.945k
BM_ExtractPaths_StandardProcessDAG/1000000/20              31.0 ms         31.0 ms           23 ChunksProcessed=3 OutputPaths=10.945k
BM_ExtractPaths_StandardProcessDAG/10000000/20             31.0 ms         31.0 ms           22 ChunksProcessed=3 OutputPaths=10.945k
BM_ExtractPaths_StandardProcessDAG/100000/25                978 ms          978 ms            1 ChunksProcessed=30 OutputPaths=121.392k
BM_ExtractPaths_StandardProcessDAG/1000000/25               964 ms          964 ms            1 ChunksProcessed=30 OutputPaths=121.392k
BM_ExtractPaths_StandardProcessDAG/10000000/25              958 ms          958 ms            1 ChunksProcessed=30 OutputPaths=121.392k
BM_ExtractPaths_StandardProcessDAG/100000/30              30773 ms        30761 ms            1 ChunksProcessed=329 OutputPaths=1.34627M
BM_ExtractPaths_StandardProcessDAG/1000000/30             29721 ms        29720 ms            1 ChunksProcessed=329 OutputPaths=1.34627M
BM_ExtractPaths_StandardProcessDAG/10000000/30            29796 ms        29795 ms            1 ChunksProcessed=329 OutputPaths=1.34627M
BM_ExtractPaths_MemoryUsage/100000/10/iterations:1        0.066 ms        0.064 ms            1 ChunksProcessed=1 LeakedMemBytes=0 OutputPaths=88 PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/1000000/10/iterations:1       0.096 ms        0.095 ms            1 ChunksProcessed=1 LeakedMemBytes=0 OutputPaths=88 PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/10000000/10/iterations:1      0.417 ms        0.415 ms            1 ChunksProcessed=1 LeakedMemBytes=0 OutputPaths=88 PeakMemBytes=2.62224M
BM_ExtractPaths_MemoryUsage/100000/15/iterations:1        0.915 ms        0.915 ms            1 ChunksProcessed=1 LeakedMemBytes=0 OutputPaths=986 PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/1000000/15/iterations:1       0.962 ms        0.961 ms            1 ChunksProcessed=1 LeakedMemBytes=0 OutputPaths=986 PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/10000000/15/iterations:1       1.25 ms         1.25 ms            1 ChunksProcessed=1 LeakedMemBytes=0 OutputPaths=986 PeakMemBytes=2.62224M
BM_ExtractPaths_MemoryUsage/100000/20/iterations:1         30.8 ms         30.8 ms            1 ChunksProcessed=3 LeakedMemBytes=0 OutputPaths=10.945k PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/1000000/20/iterations:1        30.1 ms         30.1 ms            1 ChunksProcessed=3 LeakedMemBytes=0 OutputPaths=10.945k PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/10000000/20/iterations:1       30.4 ms         30.4 ms            1 ChunksProcessed=3 LeakedMemBytes=0 OutputPaths=10.945k PeakMemBytes=4.76342M
BM_ExtractPaths_MemoryUsage/100000/25/iterations:1          973 ms          972 ms            1 ChunksProcessed=30 LeakedMemBytes=0 OutputPaths=121.392k PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/1000000/25/iterations:1         973 ms          973 ms            1 ChunksProcessed=30 LeakedMemBytes=0 OutputPaths=121.392k PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/10000000/25/iterations:1        978 ms          978 ms            1 ChunksProcessed=30 LeakedMemBytes=0 OutputPaths=121.392k PeakMemBytes=4.76342M
BM_ExtractPaths_MemoryUsage/100000/30/iterations:1        31017 ms        31011 ms            1 ChunksProcessed=329 LeakedMemBytes=0 OutputPaths=1.34627M PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/1000000/30/iterations:1       31276 ms        31272 ms            1 ChunksProcessed=329 LeakedMemBytes=0 OutputPaths=1.34627M PeakMemBytes=0
BM_ExtractPaths_MemoryUsage/10000000/30/iterations:1      29934 ms        29926 ms            1 ChunksProcessed=329 LeakedMemBytes=0 OutputPaths=1.34627M PeakMemBytes=4.76342M
BM_ExtractPaths_MultiRowDAG/1000/10/10                    0.303 ms        0.303 ms         2293 ChunksProcessed=1 OutputPaths=880
BM_ExtractPaths_MultiRowDAG/10000/10/10                   0.311 ms        0.311 ms         2259 ChunksProcessed=1 OutputPaths=880
BM_ExtractPaths_MultiRowDAG/1000/15/10                     8.68 ms         8.68 ms           80 ChunksProcessed=3 OutputPaths=9.86k
BM_ExtractPaths_MultiRowDAG/10000/15/10                    8.80 ms         8.80 ms           65 ChunksProcessed=3 OutputPaths=9.86k
BM_ExtractPaths_MultiRowDAG/1000/10/100                    2.88 ms         2.88 ms          243 ChunksProcessed=3 OutputPaths=8.8k
BM_ExtractPaths_MultiRowDAG/10000/10/100                   3.00 ms         3.00 ms          236 ChunksProcessed=3 OutputPaths=8.8k
BM_ExtractPaths_MultiRowDAG/1000/15/100                    87.1 ms         87.0 ms            8 ChunksProcessed=25 OutputPaths=98.6k
BM_ExtractPaths_MultiRowDAG/10000/15/100                   89.4 ms         89.4 ms            8 ChunksProcessed=25 OutputPaths=98.6k
BM_ExtractPaths_MultiRowDAG/1000/10/1000                   30.9 ms         30.8 ms           22 ChunksProcessed=22 OutputPaths=88k
BM_ExtractPaths_MultiRowDAG/10000/10/1000                  31.7 ms         31.7 ms           21 ChunksProcessed=22 OutputPaths=88k
BM_ExtractPaths_MultiRowDAG/1000/15/1000                    866 ms          865 ms            1 ChunksProcessed=241 OutputPaths=986k
BM_ExtractPaths_MultiRowDAG/10000/15/1000                   881 ms          881 ms            1 ChunksProcessed=241 OutputPaths=986k
*/

class MockRuntimeState : public RuntimeState {
public:
    MockRuntimeState() : RuntimeState() { set_chunk_size(4096); }
};

class ExtractPathsBenchmarkHelper {
public:
    static ColumnPtr build_graph_column(const std::vector<std::vector<int64_t>>& adj_matrix) {
        return build_graph_column_multi_row(adj_matrix, 1);
    }

    // Same layout as build_graph_column, but replicates the same adjacency list across
    // num_rows independent graph rows (the graph is read per row, so a multi-row
    // benchmark needs its own copy per row - mirroring what a real batched caller pays).
    static ColumnPtr build_graph_column_multi_row(const std::vector<std::vector<int64_t>>& adj_matrix,
                                                  size_t num_rows) {
        size_t total_edges = 0;
        for (const auto& outer : adj_matrix) {
            total_edges += outer.size();
        }

        auto neighbors_offsets = UInt32Column::create();
        neighbors_offsets->reserve(adj_matrix.size() * num_rows + 1);
        neighbors_offsets->append(0);
        auto neighbors = NullableColumn::create(RunTimeColumnType<TYPE_BIGINT>::create(), NullColumn::create());
        neighbors->reserve(total_edges * num_rows);
        auto current_offset = 0U;

        for (size_t r = 0; r < num_rows; r++) {
            for (const auto& outer : adj_matrix) {
                for (const auto& elem : outer) {
                    neighbors->append_datum(elem);
                    current_offset++;
                }
                neighbors_offsets->append(current_offset);
            }
        }

        auto neighbors_column = ArrayColumn::create(neighbors, neighbors_offsets);
        auto top_offsets = UInt32Column::create();
        top_offsets->append(0);
        for (size_t r = 1; r <= num_rows; r++) {
            top_offsets->append(static_cast<uint32_t>(r * adj_matrix.size()));
        }

        auto inner_nulls = NullColumn::create();
        inner_nulls->append_default(adj_matrix.size() * num_rows);

        auto final_nulls = NullColumn::create();
        final_nulls->append_default(num_rows);

        return NullableColumn::create(
                ArrayColumn::create(NullableColumn::create(neighbors_column, std::move(inner_nulls)),
                                    std::move(top_offsets)),
                std::move(final_nulls));
    }

    static ColumnPtr build_node_array_column(const std::vector<int64_t>& nodes) {
        return build_node_array_column_multi_row(nodes, 1);
    }

    // Same layout as build_node_array_column, but replicates the same node array across
    // num_rows independent rows, so it can seed a benchmark with many rows in one call.
    static ColumnPtr build_node_array_column_multi_row(const std::vector<int64_t>& nodes, size_t num_rows) {
        auto offsets = UInt32Column::create();
        offsets->reserve(num_rows + 1);
        offsets->append(0);
        auto elements = NullableColumn::create(RunTimeColumnType<TYPE_BIGINT>::create(), NullColumn::create());
        elements->reserve(nodes.size() * num_rows);

        uint32_t running_offset = 0;
        for (size_t r = 0; r < num_rows; r++) {
            for (int64_t n : nodes) {
                elements->append_datum(n);
                running_offset++;
            }
            offsets->append(running_offset);
        }

        auto nulls = NullColumn::create();
        nulls->append_default(num_rows);
        return NullableColumn::create(ArrayColumn::create(elements, std::move(offsets)), std::move(nulls));
    }

    static ColumnPtr build_config_column(const std::string& config_str) {
        return build_config_column_multi_row(config_str, 1);
    }

    // Same layout as build_config_column, but repeats the same config string across
    // num_rows rows (the config token is read per row, so it can't be left as a single
    // row once the other columns carry more than one).
    static ColumnPtr build_config_column_multi_row(const std::string& config_str, size_t num_rows) {
        auto str_elements = BinaryColumn::create();
        auto str_nulls = NullColumn::create();
        for (size_t r = 0; r < num_rows; r++) {
            str_elements->append(config_str);
        }
        str_nulls->append_default(num_rows);
        return NullableColumn::create(std::move(str_elements), std::move(str_nulls));
    }

    // Drives a table function through init/prepare/process/close until the input is
    // fully consumed, returning the total number of output rows produced.
    // - out_chunks_processed, when non-null, is incremented once per tf.process() call so
    //   callers can confirm how many chunks a run actually took.
    static size_t run_to_completion(TableFunction& tf, RuntimeState& runtime_state, const Columns& cols,
                                    benchmark::State& state, size_t* out_chunks_processed = nullptr) {
        size_t total_output_rows = 0;

        TableFunctionState* tf_state = nullptr;
        (void)tf.init(TFunction(), &tf_state);
        tf_state->set_params(cols);
        (void)tf.prepare(tf_state);

        while (tf_state->processed_rows() < tf_state->input_rows()) {
            auto result = tf.process(&runtime_state, tf_state);
            if (out_chunks_processed != nullptr) {
                (*out_chunks_processed)++;
            }

            if (!tf_state->status().ok()) {
                state.SkipWithError(std::string(tf_state->status().message()).c_str());
                break;
            }

            if (!result.first.empty() && result.first[0] != nullptr) {
                total_output_rows += result.first[0]->size();
            }
        }

        (void)tf.close(&runtime_state, tf_state);
        return total_output_rows;
    }

    static ColumnPtr build_null_constraints_column() { return build_null_constraints_column_multi_row(1); }

    // Same layout as build_null_constraints_column (an always-null, always-empty
    // constraints column), but with num_rows rows instead of one.
    static ColumnPtr build_null_constraints_column_multi_row(size_t num_rows) {
        auto outer_nulls = NullColumn::create();
        for (size_t r = 0; r < num_rows; r++) {
            outer_nulls->append(1);
        }

        auto inner_elements = NullableColumn::create(RunTimeColumnType<TYPE_BIGINT>::create(), NullColumn::create());
        auto inner_offsets = UInt32Column::create();
        auto inner_array = ArrayColumn::create(inner_elements, inner_offsets);

        auto outer_elements_nulls = NullColumn::create(inner_array->size(), 0);
        auto outer_elements = NullableColumn::create(inner_array, std::move(outer_elements_nulls));

        auto outer_offsets = UInt32Column::create();
        for (size_t r = 0; r <= num_rows; r++) {
            outer_offsets->append(0);
        }

        return NullableColumn::create(ArrayColumn::create(outer_elements, std::move(outer_offsets)),
                                      std::move(outer_nulls));
    }
};

// ============================================================================
// STANDARD 1: Typical Process DAG (Moderate Branching)
// Simulates mostly moving forward but with parallel branches/skips (i -> i+1, i+2).
// ============================================================================
static void BM_ExtractPaths_StandardProcessDAG(benchmark::State& state) {
    const int num_nodes = state.range(0);
    const int max_path_length = state.range(1);

    auto adj = BenchmarkGraphGenerator::generate_standard_dag_adj_list(num_nodes);

    auto graph_col = ExtractPathsBenchmarkHelper::build_graph_column(adj);
    auto start_col = ExtractPathsBenchmarkHelper::build_node_array_column({0});

    auto end_col = ExtractPathsBenchmarkHelper::build_node_array_column({static_cast<int64_t>(max_path_length)});
    auto constr_col = ExtractPathsBenchmarkHelper::build_null_constraints_column();

    std::string config_str = ":LE:" + std::to_string(max_path_length);
    auto config_col = ExtractPathsBenchmarkHelper::build_config_column(config_str);

    Columns cols{graph_col, start_col, end_col, constr_col, config_col};

    CelonisObjectLinkExtractPaths tf;
    MockRuntimeState runtime_state;

    size_t total_output_rows = 0;
    size_t total_chunks_processed = 0;

    for (auto _ : state) {
        total_output_rows +=
                ExtractPathsBenchmarkHelper::run_to_completion(tf, runtime_state, cols, state, &total_chunks_processed);
    }

    state.counters["OutputPaths"] = benchmark::Counter(total_output_rows, benchmark::Counter::kAvgIterations);
    state.counters["ChunksProcessed"] = benchmark::Counter(total_chunks_processed, benchmark::Counter::kAvgIterations);
}

BENCHMARK(BM_ExtractPaths_StandardProcessDAG)
        ->ArgsProduct({
                {100'000, 1'000'000, 10'000'000}, // num_nodes
                {10, 15, 20, 25, 30}              // max_path_length
        })
        ->Unit(benchmark::kMillisecond);

// ============================================================================
// MEMORY: Peak and residual memory consumption of a full extraction run.
//
// Attaches a dedicated MemTracker to the current thread for the duration of each
// iteration, so every allocation/deallocation made by init/prepare/process/close
// (path stacks, frontier, bitsets, output columns, ...) is captured through the
// global jemalloc hook (see mem_hook.cpp). PeakMemBytes is the tracker's high-water
// mark for the run; LeakedMemBytes is what's still attributed to the tracker after
// close() - it should be ~0, and a persistently non-zero value points at a leak in
// the table function's state teardown.
//
// NOTE: this measurement only reflects real memory when the binary is built without
// BE_TEST (i.e. a plain WITH_BENCH=ON build) - a MAKE_TEST=ON build routes the mem
// hook through a plain atomic counter instead of the CurrentThread tracker used here.
//
// It also requires GlobalEnv to be initialized: CurrentThread::mem_tracker() - which
// every cached/uncached mem_consume path in mem_hook.cpp actually calls - returns
// nullptr unconditionally while GlobalEnv::is_init() is false, regardless of what
// set_mem_tracker() below has set tls_mem_tracker to. main() (below) initializes
// GlobalEnv once at startup so this tracker actually receives the hook's calls.
//
// PeakMemBytes reads 0 for smaller graphs - this is expected, not a bug. Allocations
// don't reach the tracker directly: CurrentThread::MemCacheManager (current_thread.h)
// nets them into a per-thread cache and only calls MemTracker::consume() - the call
// that updates the high-water mark peak_consumption() reads - once the cache's net
// delta crosses +/-BATCH_SIZE (2MB). A run whose live footprint never crosses that
// threshold only ever commits its small net-of-everything remainder at teardown
// (via set_mem_tracker(nullptr)'s mem_tracker_ctx_shift()), so no intermediate peak
// is ever observed. This mirrors production: a real query under the 2MB batching
// threshold is just as invisible to peak_consumption(). LeakedMemBytes is unaffected
// (that same teardown commit always reconciles it to the true residual) and is the
// signal this benchmark primarily exists to catch regressions in.
// ============================================================================
static void BM_ExtractPaths_MemoryUsage(benchmark::State& state) {
    const int num_nodes = state.range(0);
    const int max_path_length = state.range(1);

    auto adj = BenchmarkGraphGenerator::generate_standard_dag_adj_list(num_nodes);

    auto graph_col = ExtractPathsBenchmarkHelper::build_graph_column(adj);
    auto start_col = ExtractPathsBenchmarkHelper::build_node_array_column({0});

    auto end_col = ExtractPathsBenchmarkHelper::build_node_array_column({static_cast<int64_t>(max_path_length)});
    auto constr_col = ExtractPathsBenchmarkHelper::build_null_constraints_column();

    std::string config_str = ":LE:" + std::to_string(max_path_length);
    auto config_col = ExtractPathsBenchmarkHelper::build_config_column(config_str);

    Columns cols{graph_col, start_col, end_col, constr_col, config_col};

    CelonisObjectLinkExtractPaths tf;
    MockRuntimeState runtime_state;

    size_t total_output_rows = 0;
    size_t total_chunks_processed = 0;
    int64_t peak_bytes = 0;
    int64_t leaked_bytes = 0;

    for (auto _ : state) {
        MemTracker tracker;
        CurrentThread::current().set_mem_tracker(&tracker);

        total_output_rows +=
                ExtractPathsBenchmarkHelper::run_to_completion(tf, runtime_state, cols, state, &total_chunks_processed);

        CurrentThread::current().set_mem_tracker(nullptr);
        peak_bytes = tracker.peak_consumption();
        leaked_bytes = tracker.consumption();
    }

    state.counters["OutputPaths"] = benchmark::Counter(total_output_rows, benchmark::Counter::kAvgIterations);
    state.counters["ChunksProcessed"] = benchmark::Counter(total_chunks_processed, benchmark::Counter::kAvgIterations);
    state.counters["PeakMemBytes"] = benchmark::Counter(static_cast<double>(peak_bytes));
    state.counters["LeakedMemBytes"] = benchmark::Counter(static_cast<double>(leaked_bytes));
}

BENCHMARK(BM_ExtractPaths_MemoryUsage)
        ->ArgsProduct({
                {100'000, 1'000'000, 10'000'000}, // num_nodes
                {10, 15, 20, 25, 30}              // max_path_length
        })
        ->Unit(benchmark::kMillisecond)
        ->Iterations(1);

// ============================================================================
// MULTI-ROW: many independent single-start-node rows sharing one (replicated) graph.
//
// This exercises a different multi-chunk trigger than BM_ExtractPaths_StandardProcessDAG
// above: there, one row's own output exceeds chunk_size (4096) and process() suspends and
// resumes the *same* row's DFS stack across calls. Here every row's own output is small
// (row_done is reached quickly), so what forces extra process() calls is the sheer number
// of rows - process() keeps consuming whole rows into the current chunk until chunk_size
// is hit, then returns and picks up with the next row on the following call. ChunksProcessed
// reports how many tf.process() calls each run actually took, so the multi-chunk behavior
// is verified rather than just inferred from the argument grid.
//
// num_nodes is kept small here (unlike BM_ExtractPaths_StandardProcessDAG's up-to-10M grid):
// the graph is physically replicated once per row (mirroring what a real batched caller
// pays), and BM_ExtractPaths_StandardProcessDAG's own captured baseline above shows runtime
// barely varies with num_nodes for a fixed max_path_length - so a large graph would just
// multiply memory (and setup time) by num_rows for no benchmarking benefit.
// ============================================================================
static void BM_ExtractPaths_MultiRowDAG(benchmark::State& state) {
    const int num_nodes = state.range(0);
    const int max_path_length = state.range(1);
    const size_t num_rows = state.range(2);

    auto adj = BenchmarkGraphGenerator::generate_standard_dag_adj_list(num_nodes);

    auto graph_col = ExtractPathsBenchmarkHelper::build_graph_column_multi_row(adj, num_rows);
    auto start_col = ExtractPathsBenchmarkHelper::build_node_array_column_multi_row({0}, num_rows);
    auto end_col = ExtractPathsBenchmarkHelper::build_node_array_column_multi_row(
            {static_cast<int64_t>(max_path_length)}, num_rows);
    auto constr_col = ExtractPathsBenchmarkHelper::build_null_constraints_column_multi_row(num_rows);

    std::string config_str = ":LE:" + std::to_string(max_path_length);
    auto config_col = ExtractPathsBenchmarkHelper::build_config_column_multi_row(config_str, num_rows);

    Columns cols{graph_col, start_col, end_col, constr_col, config_col};

    CelonisObjectLinkExtractPaths tf;
    MockRuntimeState runtime_state;

    size_t total_output_rows = 0;
    size_t total_chunks_processed = 0;

    for (auto _ : state) {
        total_output_rows +=
                ExtractPathsBenchmarkHelper::run_to_completion(tf, runtime_state, cols, state, &total_chunks_processed);
    }

    state.counters["OutputPaths"] = benchmark::Counter(total_output_rows, benchmark::Counter::kAvgIterations);
    state.counters["ChunksProcessed"] = benchmark::Counter(total_chunks_processed, benchmark::Counter::kAvgIterations);
}

BENCHMARK(BM_ExtractPaths_MultiRowDAG)
        ->ArgsProduct({
                {1'000, 10'000},  // num_nodes
                {10, 15},         // max_path_length
                {10, 100, 1'000}, // num_rows
        })
        ->Unit(benchmark::kMillisecond);

} // namespace starrocks

// A plain benchmark binary never runs the BE server startup path, so GlobalEnv is
// otherwise never initialized. Without it, CurrentThread::mem_tracker() - which every
// mem_consume/mem_release path in mem_hook.cpp goes through - returns nullptr no
// matter what BM_ExtractPaths_MemoryUsage's set_mem_tracker() call has set
// tls_mem_tracker to, so its MemTracker would silently never see any allocation.
int main(int argc, char** argv) {
    starrocks::MemInfo::init();
    // Config fields (e.g. mem_limit, used by GlobalEnv::init() below) are declared with an
    // empty default storage value - the "90%"-style defaults registered via CONF_String etc.
    // are only actually applied by config::init(), which a plain benchmark binary otherwise
    // never calls. Passing an empty stream applies just the defaults, with nothing to override.
    std::istringstream empty_conf("");
    starrocks::config::init(empty_conf);

    auto status = starrocks::GlobalEnv::GetInstance()->init();
    if (!status.ok()) {
        std::cerr << "Failed to initialize GlobalEnv: " << status.message() << std::endl;
        return 1;
    }

    char arg0_default[] = "benchmark";
    char* args_default = arg0_default;
    if (!argv) {
        argc = 1;
        argv = &args_default;
    }
    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}