#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <vector>

#include "bench/celonis/bench_graph_generator.h"
#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/type_traits.h"
#include "exprs/celonis/table_function/object_link_extract_paths.h"
#include "runtime/runtime_state.h"

namespace starrocks {

/*
2026-07-28T09:37:45+00:00
Running ./be/build_Release/src/bench/celonis/output/object_link_extract_paths_bench
Run on (16 X 4900 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 1280 KiB (x8)
  L3 Unified 24576 KiB (x1)
Load Average: 1.66, 2.99, 3.50
---------------------------------------------------------------------------------------------------------
Benchmark                                               Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------------
BM_ExtractPaths_StandardProcessDAG/100000/10        0.029 ms        0.029 ms        24338 OutputPaths=88
BM_ExtractPaths_StandardProcessDAG/1000000/10       0.035 ms        0.035 ms        19613 OutputPaths=88
BM_ExtractPaths_StandardProcessDAG/10000000/10      0.125 ms        0.125 ms         5532 OutputPaths=88
BM_ExtractPaths_StandardProcessDAG/100000/15        0.849 ms        0.849 ms          819 OutputPaths=986
BM_ExtractPaths_StandardProcessDAG/1000000/15       0.861 ms        0.860 ms          838 OutputPaths=986
BM_ExtractPaths_StandardProcessDAG/10000000/15      0.936 ms        0.936 ms          739 OutputPaths=986
BM_ExtractPaths_StandardProcessDAG/100000/20         26.3 ms         26.3 ms           27 OutputPaths=10.945k
BM_ExtractPaths_StandardProcessDAG/1000000/20        26.4 ms         26.4 ms           27 OutputPaths=10.945k
BM_ExtractPaths_StandardProcessDAG/10000000/20       26.6 ms         26.6 ms           27 OutputPaths=10.945k
BM_ExtractPaths_StandardProcessDAG/100000/25          837 ms          837 ms            1 OutputPaths=121.392k
BM_ExtractPaths_StandardProcessDAG/1000000/25         827 ms          827 ms            1 OutputPaths=121.392k
BM_ExtractPaths_StandardProcessDAG/10000000/25        821 ms          821 ms            1 OutputPaths=121.392k
BM_ExtractPaths_StandardProcessDAG/100000/30        26753 ms        26744 ms            1 OutputPaths=1.34627M
BM_ExtractPaths_StandardProcessDAG/1000000/30       27858 ms        27856 ms            1 OutputPaths=1.34627M
BM_ExtractPaths_StandardProcessDAG/10000000/30      26621 ms        26617 ms            1 OutputPaths=1.34627M
*/

class MockRuntimeState : public RuntimeState {
public:
    MockRuntimeState() : RuntimeState() { set_chunk_size(4096); }
};

class ExtractPathsBenchmarkHelper {
public:
    static ColumnPtr build_graph_column(const std::vector<std::vector<int64_t>>& adj_matrix) {
        auto neighbors_offsets = UInt32Column::create();
        neighbors_offsets->append(0);
        auto neighbors = NullableColumn::create(RunTimeColumnType<TYPE_BIGINT>::create(), NullColumn::create());
        auto current_offset = 0U;

        for (const auto& outer : adj_matrix) {
            for (const auto& elem : outer) {
                neighbors->append_datum(elem);
                current_offset++;
            }
            neighbors_offsets->append(current_offset);
        }

        auto neighbors_column = ArrayColumn::create(neighbors, neighbors_offsets);
        auto top_offsets = UInt32Column::create();
        top_offsets->append(0);
        top_offsets->append(adj_matrix.size());

        auto inner_nulls = NullColumn::create();
        inner_nulls->append_default(adj_matrix.size());

        auto final_nulls = NullColumn::create();
        final_nulls->append(0);

        return NullableColumn::create(
                ArrayColumn::create(NullableColumn::create(neighbors_column, std::move(inner_nulls)),
                                    std::move(top_offsets)),
                std::move(final_nulls));
    }

    static ColumnPtr build_node_array_column(const std::vector<int64_t>& nodes) {
        auto offsets = UInt32Column::create();
        offsets->append(0);
        auto elements = NullableColumn::create(RunTimeColumnType<TYPE_BIGINT>::create(), NullColumn::create());

        for (int64_t n : nodes) {
            elements->append_datum(n);
        }
        offsets->append(nodes.size());

        auto nulls = NullColumn::create();
        nulls->append(0);
        return NullableColumn::create(ArrayColumn::create(elements, std::move(offsets)), std::move(nulls));
    }

    static ColumnPtr build_config_column(const std::string& config_str) {
        auto str_elements = BinaryColumn::create();
        auto str_nulls = NullColumn::create();
        str_elements->append(config_str);
        str_nulls->append(0);
        return NullableColumn::create(std::move(str_elements), std::move(str_nulls));
    }

    static ColumnPtr build_null_constraints_column() {
        auto outer_nulls = NullColumn::create();
        outer_nulls->append(1);

        auto inner_elements = NullableColumn::create(RunTimeColumnType<TYPE_BIGINT>::create(), NullColumn::create());
        auto inner_offsets = UInt32Column::create();
        auto inner_array = ArrayColumn::create(inner_elements, inner_offsets);

        auto outer_elements_nulls = NullColumn::create(inner_array->size(), 0);
        auto outer_elements = NullableColumn::create(inner_array, std::move(outer_elements_nulls));

        auto outer_offsets = UInt32Column::create();
        outer_offsets->append(0);
        outer_offsets->append(0);

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

    for (auto _ : state) {
        TableFunctionState* tf_state = nullptr;
        (void)tf.init(TFunction(), &tf_state);
        tf_state->set_params(cols);
        (void)tf.prepare(tf_state);

        while (tf_state->processed_rows() < tf_state->input_rows()) {
            auto result = tf.process(&runtime_state, tf_state);

            if (!tf_state->status().ok()) {
                state.SkipWithError(std::string(tf_state->status().message()).c_str());
                break;
            }

            if (!result.first.empty() && result.first[0] != nullptr) {
                total_output_rows += result.first[0]->size();
            }
        }

        (void)tf.close(&runtime_state, tf_state);
    }

    state.counters["OutputPaths"] = benchmark::Counter(total_output_rows, benchmark::Counter::kAvgIterations);
}

BENCHMARK(BM_ExtractPaths_StandardProcessDAG)
        ->ArgsProduct({
                {100'000, 1'000'000, 10'000'000}, // num_nodes
                {10, 15, 20, 25, 30}              // max_path_length
        })
        ->Unit(benchmark::kMillisecond);

} // namespace starrocks

BENCHMARK_MAIN();