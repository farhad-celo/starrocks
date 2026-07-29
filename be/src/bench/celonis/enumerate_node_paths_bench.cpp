#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <vector>

#include "bench_graph_generator.h"
#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/const_column.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "common/config.h"
#include "exprs/celonis/agg/enumerate_node_paths.h"
#include "exprs/function_context.h"
#include "runtime/runtime_state.h"
#include "types/logical_type.h"

namespace starrocks {

/*
2026-07-28T09:33:37+00:00
Running ./be/build_Release/src/bench/celonis/output/enumerate_node_paths_bench
Run on (16 X 4900 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 1280 KiB (x8)
  L3 Unified 24576 KiB (x1)
Load Average: 6.19, 4.57, 4.02
---------------------------------------------------------------------------------------------------------------
Benchmark                                                     Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------------------
BM_EnumerateAggregate_StandardProcessDAG/100000/10         46.2 ms         46.2 ms           11 FinalizeCalls=2 OutputPaths=88
BM_EnumerateAggregate_StandardProcessDAG/1000000/10         656 ms          656 ms            1 FinalizeCalls=2 OutputPaths=88
BM_EnumerateAggregate_StandardProcessDAG/10000000/10       9295 ms         9293 ms            1 FinalizeCalls=2 OutputPaths=88
BM_EnumerateAggregate_StandardProcessDAG/100000/15         44.8 ms         44.8 ms           16 FinalizeCalls=2 OutputPaths=986
BM_EnumerateAggregate_StandardProcessDAG/1000000/15         724 ms          724 ms            1 FinalizeCalls=2 OutputPaths=986
BM_EnumerateAggregate_StandardProcessDAG/10000000/15       9331 ms         9330 ms            1 FinalizeCalls=2 OutputPaths=986
BM_EnumerateAggregate_StandardProcessDAG/100000/20         75.1 ms         75.1 ms            9 FinalizeCalls=4 OutputPaths=10.945k
BM_EnumerateAggregate_StandardProcessDAG/1000000/20         758 ms          754 ms            1 FinalizeCalls=4 OutputPaths=10.945k
BM_EnumerateAggregate_StandardProcessDAG/10000000/20       9351 ms         9343 ms            1 FinalizeCalls=4 OutputPaths=10.945k
BM_EnumerateAggregate_StandardProcessDAG/100000/25          946 ms          945 ms            1 FinalizeCalls=31 OutputPaths=121.392k
BM_EnumerateAggregate_StandardProcessDAG/1000000/25        1563 ms         1563 ms            1 FinalizeCalls=31 OutputPaths=121.392k
BM_EnumerateAggregate_StandardProcessDAG/10000000/25      10236 ms        10233 ms            1 FinalizeCalls=31 OutputPaths=121.392k
BM_EnumerateAggregate_StandardProcessDAG/100000/30        26027 ms        26007 ms            1 FinalizeCalls=330 OutputPaths=1.34627M
BM_EnumerateAggregate_StandardProcessDAG/1000000/30       26335 ms        26298 ms            1 FinalizeCalls=330 OutputPaths=1.34627M
BM_EnumerateAggregate_StandardProcessDAG/10000000/30      34478 ms        34473 ms            1 FinalizeCalls=330 OutputPaths=1.34627M
*/

class EnumeratePathsBenchmarkHelper {
public:
    static ColumnPtr build_struct_column(const std::vector<int64_t>& vals) {
        auto elements = RunTimeColumnType<TYPE_BIGINT>::create();
        for (auto v : vals) {
            elements->append_datum(v);
        }
        Columns fields;
        fields.push_back(std::move(elements));
        return StructColumn::create(std::move(fields), {"id"});
    }

    static ColumnPtr build_boolean_column(const std::vector<uint8_t>& vals) {
        auto col = RunTimeColumnType<TYPE_BOOLEAN>::create();
        for (auto v : vals) {
            col->append_datum(v);
        }
        return col;
    }

    static ColumnPtr build_const_boolean(bool val, size_t size) {
        auto col = RunTimeColumnType<TYPE_BOOLEAN>::create();
        col->append_datum(val);
        return ConstColumn::create(std::move(col), size);
    }

    static ColumnPtr build_const_varchar(const std::string& val, size_t size) {
        auto col = RunTimeColumnType<TYPE_VARCHAR>::create();
        col->append_datum(Slice(val));
        return ConstColumn::create(std::move(col), size);
    }

    static ColumnPtr build_const_bigint(int64_t val, size_t size) {
        auto col = RunTimeColumnType<TYPE_BIGINT>::create();
        col->append_datum(val);
        return ConstColumn::create(std::move(col), size);
    }

    static std::unique_ptr<FunctionContext> create_dummy_context(int max_path_length) {
        TypeDescriptor struct_type(TYPE_STRUCT);
        struct_type.children.push_back(TypeDescriptor(TYPE_BIGINT));

        std::vector<TypeDescriptor> arg_types(12, TypeDescriptor(TYPE_BOOLEAN));
        arg_types[0] = struct_type;                   // OUT_COLUMNS
        arg_types[1] = struct_type;                   // IN_COLUMNS
        arg_types[2] = struct_type;                   // PK_COLUMNS
        arg_types[10] = TypeDescriptor(TYPE_VARCHAR); // LENGTH_COMPARISON
        arg_types[11] = TypeDescriptor(TYPE_BIGINT);  // LENGTH

        TypeDescriptor return_type(TYPE_STRUCT);
        auto ctx = std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));

        Columns constant_columns;
        constant_columns.resize(12); // Must match argument count
        constant_columns[9] = build_const_boolean(false, 1);
        constant_columns[10] = build_const_varchar("LESS_EQUAL", 1);
        constant_columns[11] = build_const_bigint(max_path_length, 1);

        ctx->set_constant_columns(std::move(constant_columns));

        return ctx;
    }

    static ColumnPtr build_output_column() {
        auto out_array_elements = RunTimeColumnType<TYPE_BIGINT>::create();
        auto out_array = ArrayColumn::create(out_array_elements, UInt32Column::create());

        Columns to_fields;
        to_fields.push_back(std::move(out_array));
        return StructColumn::create(std::move(to_fields), {"id"});
    }
};

// ============================================================================
// STANDARD 1: Typical Process DAG (Moderate Branching)
// Simulates mostly moving forward but with parallel branches/skips (i -> i+1, i+2).
// ============================================================================
static void BM_EnumerateAggregate_StandardProcessDAG(benchmark::State& state) {
    const int num_nodes = state.range(0);
    const int max_path_length = state.range(1);

    auto graph = BenchmarkGraphGenerator::generate_standard_dag_edge_list(num_nodes);
    const auto& out_vals = graph.out_vals;
    const auto& in_vals = graph.in_vals;
    const auto& pk_vals = graph.pk_vals;

    std::vector<uint8_t> out_start_vals, out_end_vals, in_start_vals, in_end_vals;
    std::vector<uint8_t> out_all_vals, in_all_vals;

    const int64_t end_node = max_path_length;
    size_t num_edges = out_vals.size();

    for (size_t i = 0; i < num_edges; ++i) {
        out_start_vals.push_back(out_vals[i] == 0);
        out_end_vals.push_back(out_vals[i] == end_node);
        in_start_vals.push_back(in_vals[i] == 0);
        in_end_vals.push_back(in_vals[i] == end_node);
        out_all_vals.push_back(1);
        in_all_vals.push_back(1);
    }

    Columns cols;
    cols.push_back(EnumeratePathsBenchmarkHelper::build_struct_column(out_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_struct_column(in_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_struct_column(pk_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_boolean_column(out_start_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_boolean_column(out_end_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_boolean_column(in_start_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_boolean_column(in_end_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_boolean_column(out_all_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_boolean_column(in_all_vals));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_const_boolean(false, num_edges));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_const_varchar("LESS_EQUAL", num_edges));
    cols.push_back(EnumeratePathsBenchmarkHelper::build_const_bigint(max_path_length, num_edges));

    std::vector<const Column*> raw_columns;
    raw_columns.reserve(cols.size());
    for (const auto& c : cols) {
        raw_columns.push_back(c.get());
    }

    auto ctx = EnumeratePathsBenchmarkHelper::create_dummy_context(max_path_length);
    CelonisEnumerateAggregateFunction agg_func(CelonisEnumerateAggregateFunction::Mode::NODE_PATHS);

    size_t total_finalize_calls = 0;
    size_t total_output_rows = 0;

    for (auto _ : state) {
        // 1. Pause the benchmark timer to exclude initialization and aggregation pushdown
        state.PauseTiming();

        alignas(CelonisEnumerateAggregateState) uint8_t state_buf[sizeof(CelonisEnumerateAggregateState)];
        AggDataPtr agg_state = state_buf;

        auto* state_impl = new (agg_state) CelonisEnumerateAggregateState();

        agg_func.update_batch_single_state(ctx.get(), num_edges, raw_columns.data(), agg_state);

        // 2. Resume the timer specifically for finalize_to_column
        state.ResumeTiming();

        bool is_eos = false;
        while (!is_eos) {
            auto to_column = EnumeratePathsBenchmarkHelper::build_output_column();
            agg_func.finalize_to_column(ctx.get(), agg_state, to_column.get());
            size_t result_rows = to_column->size();
            total_output_rows += result_rows;
            ++total_finalize_calls;
            is_eos = (result_rows <= 1);
        }

        // 3. Pause again during teardown
        state.PauseTiming();
        state_impl->~CelonisEnumerateAggregateState();
        state.ResumeTiming();
    }
    state.counters["FinalizeCalls"] = benchmark::Counter(total_finalize_calls, benchmark::Counter::kAvgIterations);
    state.counters["OutputPaths"] = benchmark::Counter(total_output_rows, benchmark::Counter::kAvgIterations);
}

BENCHMARK(BM_EnumerateAggregate_StandardProcessDAG)
        ->ArgsProduct({
                {100'000, 1'000'000, 10'000'000}, // num_nodes
                {10, 15, 20, 25, 30}              // max_path_length
        })
        ->Unit(benchmark::kMillisecond);

} // namespace starrocks

int main(int argc, char** argv) {
    // Set chunk size for this specific benchmark to map runtime execution conditions
    starrocks::config::vector_chunk_size = 4096;

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();

    return 0;
}