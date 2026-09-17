#pragma once

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "exprs/table_function/table_function.h"

namespace starrocks {

struct LinkPathConfig {
    int min_nodes = 0;
    int max_nodes = std::numeric_limits<int>::max();
    bool exclude_range = false;
    bool allow_cycles = false;
    int desired_prefix_count = 0;
};

class CelonisObjectLinkExtractPaths final : public TableFunction {
    using InputCppType = RunTimeCppType<TYPE_BIGINT>;
    using InputColumnType = RunTimeColumnType<TYPE_BIGINT>;
    using OffsetCppType = RunTimeCppType<TYPE_UNSIGNED_INT>;
    using OffsetColumnType = RunTimeColumnType<TYPE_UNSIGNED_INT>;
    using OutputColumnType = RunTimeColumnType<TYPE_BIGINT>;

    struct StackFrame {
        InputCppType node_id;
        size_t offset;
        bool evaluated;
    };

    struct GraphView {
        const InputCppType* neighbor_ids = nullptr;
        const OffsetCppType* offsets = nullptr;
        size_t num_nodes = 0;
        const InputCppType* constrained_edges = nullptr;
        size_t edge_base = 0;
    };

    struct VisitedNodeInputs {
        bool present = false;
        const OffsetCppType* row_offsets = nullptr;
        const OffsetCppType* node_offsets = nullptr;
        const InputCppType* nodes = nullptr;
    };

    // graph_column, end_node_column, constraint_column, and config_column are typically the same
    // value on every row of a chunk, unlike start_node_column and visited_node_column. When the
    // planner delivers such an argument as a ConstColumn, param_row_index()/has_any_value() (see
    // object_link_extract_paths.cpp) always read index 0 from it instead of `row`, and
    // ColumnHelper::get_data_column() unwraps a ConstColumn to its single-row backing column
    // without duplicating it - so a constant argument is never physically expanded to input_rows
    // copies anywhere in this file.
    struct ChunkInputs {
        const Column* graph_column = nullptr;
        const Column* start_node_column = nullptr;
        const Column* end_node_column = nullptr;
        const Column* constraint_column = nullptr;
        const Column* config_column = nullptr;
        const Column* visited_node_column = nullptr;

        const OffsetCppType* graph_row_offsets = nullptr;
        const OffsetCppType* graph_node_offsets = nullptr;
        const InputCppType* neighbor_ids = nullptr;

        const OffsetCppType* end_node_row_offsets = nullptr;
        const InputCppType* end_nodes = nullptr;
        bool end_nodes_have_nulls = false;

        const OffsetCppType* start_node_row_offsets = nullptr;
        const InputCppType* start_nodes = nullptr;
        bool start_nodes_have_nulls = false;

        bool has_constraints = false;
        const OffsetCppType* constraint_row_offsets = nullptr;
        const OffsetCppType* constraint_node_offsets = nullptr;
        const InputCppType* constraint_edges = nullptr;

        VisitedNodeInputs visited;
    };

    struct RowInputs {
        GraphView graph;

        const InputCppType* start_nodes = nullptr;
        InputCppType single_start_node = 0;
        bool scalar_start_node = false;
        size_t num_start_nodes = 0;
        bool start_nodes_null = false;

        const InputCppType* end_nodes = nullptr;
        size_t num_end_nodes = 0;

        std::string_view config_str;

        const OffsetCppType* visited_offsets = nullptr;
        const InputCppType* visited_nodes = nullptr;

        [[nodiscard]] InputCppType start_node(const size_t index) const {
            return scalar_start_node ? single_start_node : start_nodes[index];
        }
    };

    struct EndNodeCache {
        const InputCppType* nodes = nullptr;
        size_t num_nodes = 0;
        size_t graph_num_nodes = 0;
        bool valid = false;
    };

    struct MyState : public TableFunctionState {
        bool is_row_initialized = false;
        size_t current_start_node_id = 0;

        std::vector<StackFrame> dfs_stack;
        std::vector<InputCppType> current_path;
        boost::dynamic_bitset<> path_visited;
        boost::dynamic_bitset<> pre_visited;
        std::vector<InputCppType> pre_visited_nodes;

        bool search_done = false;
        std::vector<std::vector<InputCppType>> frontier;
        std::vector<std::vector<InputCppType>> terminal_paths;
        std::vector<std::vector<InputCppType>> materialized_paths;
        size_t emit_cursor = 0;
        size_t complete_count = 0;

        boost::dynamic_bitset<> is_end_node;
        LinkPathConfig config;

        EndNodeCache end_node_cache;
        std::string cached_config_str;
        bool has_cached_config = false;

    private:
        void on_new_params() override {
            is_row_initialized = false;
            end_node_cache.valid = false;
        }
    };

    struct PathOutputWriter {
        OutputColumnType* path_elements;
        OffsetColumnType* path_offsets;
        Int64Column* path_length;
        Int64Column* resume_node;
        NullColumn* resume_node_nulls;

        void append(const std::vector<InputCppType>& path, bool is_complete) const {
            for (const auto node : path) {
                path_elements->append(node);
            }
            path_offsets->append(static_cast<OffsetCppType>(path_elements->size()));
            if (path_length == nullptr) {
                return;
            }
            path_length->append(static_cast<int64_t>(path.size()));
            if (is_complete) {
                resume_node->append(0);
                resume_node_nulls->append(1);
            } else {
                resume_node->append(path.back());
                resume_node_nulls->append(0);
            }
        }
    };

    struct EmitContext {
        size_t chunk_limit;
        size_t chunks_added;
        const PathOutputWriter& writer;
        bool row_done;
    };

    [[nodiscard]] static Status expand_one_hop(const std::vector<std::vector<InputCppType>>& frontier,
                                               const LinkPathConfig& config, const GraphView& graph,
                                               const boost::dynamic_bitset<>& is_end_node,
                                               std::vector<std::vector<InputCppType>>& next_frontier,
                                               std::vector<std::vector<InputCppType>>& terminal);

    [[nodiscard]] Status extract_chunk_inputs(TableFunctionState* state, ChunkInputs* inputs) const;

    [[nodiscard]] static Status resolve_constrained_edges(const ChunkInputs& inputs, size_t row, size_t node_base,
                                                          size_t num_nodes, size_t edge_base,
                                                          const InputCppType** constrained_edges);

    [[nodiscard]] Status resolve_visited_nodes(const ChunkInputs& inputs, size_t row, RowInputs* row_inputs) const;

    [[nodiscard]] Status prepare_row(const ChunkInputs& inputs, size_t row, RowInputs* row_inputs) const;

    [[nodiscard]] Status begin_row(MyState* state, const ChunkInputs& inputs, const RowInputs& row_inputs) const;

    [[nodiscard]] static Status resolve_end_nodes(MyState* state, const RowInputs& row_inputs, size_t num_nodes);

    [[nodiscard]] static Status resolve_config(MyState* state, std::string_view config_str);

    static void clear_node_bitset(boost::dynamic_bitset<>& bitset, size_t num_nodes,
                                  const std::vector<InputCppType>& set_nodes);

    [[nodiscard]] static Status run_prefix_count_search(const RuntimeState* runtime_state, MyState* state,
                                                        const RowInputs& row_inputs, const GraphView& graph,
                                                        size_t num_nodes);

    [[nodiscard]] static Status run_prefix_count_mode(RuntimeState* runtime_state, MyState* state,
                                                      const RowInputs& row_inputs, const GraphView& graph,
                                                      EmitContext& emit);

    [[nodiscard]] static Status start_next_dfs_frame(MyState* state, const RowInputs& row_inputs,
                                                     const GraphView& graph, size_t num_nodes);

    [[nodiscard]] static Status advance_or_pop_dfs_frame(MyState* state, const GraphView& graph, size_t num_nodes);

    static void evaluate_dfs_frame(MyState* state, const GraphView& graph, const PathOutputWriter& writer,
                                   size_t& chunks_added);

    [[nodiscard]] static Status run_dfs_mode(MyState* state, const RowInputs& row_inputs, const GraphView& graph,
                                             size_t num_start_nodes, EmitContext& emit);

    const bool _row_per_start_node;
    const bool _expanded_output;

public:
    explicit CelonisObjectLinkExtractPaths(const bool row_per_start_node = false, const bool expanded_output = true)
            : _row_per_start_node(row_per_start_node), _expanded_output(expanded_output) {}

    [[nodiscard]] Status init(const TFunction& fn, TableFunctionState** state) const override;

    [[nodiscard]] Status prepare(TableFunctionState* state) const override { return Status::OK(); }

    [[nodiscard]] Status open(RuntimeState* runtime_state, TableFunctionState* state) const override {
        return Status::OK();
    }

    /**
     * The process method has two modes, selected by the 'PC<count>' token of the config string:
     *
     * - Legacy mode (no prefix count requested, desired_prefix_count == 0): performs a suspended
     *   DFS/backtracking traversal that enumerates only complete paths, i.e. paths reaching a
     *   flagged end node while satisfying the length constraint. Execution yields intermediate
     *   results up to the execution engine's chunk size to bound memory usage.
     *
     * - Prefix count mode (desired_prefix_count > 0): eagerly runs a count-driven breadth-first
     *   search that grows a frontier of partial paths one hop at a time (see expand_one_hop).
     *   Round 0's frontier is the start nodes themselves as length-1 paths; every later round
     *   extends only what survived the previous round, so nothing already resolved is redone. The
     *   search stops as soon as enough rows (completed paths + still-growing prefixes) have
     *   accumulated, when the frontier runs dry, or when a hard round cap is hit. The resulting
     *   rows - complete paths plus prefixes of possibly-valid paths, which a caller can resume
     *   traversal from - are then streamed out through the same chunked output contract.
     *
     *   The stop check runs after each round, never on the bare seeds, so at least one hop is
     *   always performed even when the start nodes alone would already meet the requested count:
     *   every emitted prefix is strictly longer than the start node it grew from, which is what
     *   lets a caller resume from prefixes and make progress. The search is cancellation-aware
     *   between rounds, since all of it happens within a single process() call.
     *
     * Output columns:
     * 1. path_col (ARRAY<BIGINT>): the emitted path/prefix, one row per path.
     * 2. path_length (BIGINT): the node count of the path, i.e. path_col's array length.
     * 3. resume_node (BIGINT, nullable): NULL if the row is a finished path terminating at a
     *    valid end node with valid length; otherwise (a still-growing prefix, only possible in
     *    prefix count mode) the prefix's own end node (path_col's last element), i.e. the node id
     *    a caller should pass as the corresponding start_nodes entry to resume traversal from this
     *    prefix in a later call. Every row emitted by legacy mode is complete, so this column is
     *    always NULL in legacy mode.
     *
     * When _expanded_output is false, only path_col is returned (this is the original,
     * pre-existing output contract, preserved for backward compatibility with callers registered
     * under that arity). A 'PC<count>' config token is accepted under this contract too - path_col
     * alone was always sufficient to consume its output, since a caller not passing/reading
     * resume_node only ever needs the prefix itself; only new callers that want to resume traversal
     * from an in-progress prefix need the expanded signature.
     *
     * There are two input layouts, chosen by the signature the caller used (see
     * table_function_factory.cpp) and distinguished here by _row_per_start_node:
     *
     * - Array layout (start_nodes ARRAY<BIGINT>): a group of start nodes is passed in a single
     *   array. Any number of input rows is accepted, each carrying its own array and emitting its own
     *   group of output rows, so a caller is free to batch the start nodes into as many rows as it
     *   wants parallelism from. Batching is what keeps the arguments that don't vary per start node -
     *   the graph above all - from being physically replicated once per start node: joining a
     *   constant graph onto b batch rows costs b copies of it, whereas the row-per-start-node layout
     *   below costs one copy per start node. The optional visited_nodes argument is an
     *   ARRAY<ARRAY<BIGINT>> aligned by position with the row's own start_nodes: visited_nodes[i]
     *   lists node ids to treat as already-visited (and thus blocked from re-entry when cycles are
     *   disallowed) for the traversal seeded from start_nodes[i]. This supports a two-call handover,
     *   where a prior prefix-count-mode call's own path_col for a prefix is passed straight through
     *   as the corresponding entry here, so continuing the traversal in a second legacy-mode call
     *   doesn't silently reintroduce a cycle spanning both calls.
     *
     * - Row-per-start-node layout (start_node BIGINT, visited_nodes ARRAY<BIGINT>): one input row
     *   carries one start node together with that start node's own already-visited node ids, so the
     *   handover above no longer has to funnel the resume set through array_agg and the execution
     *   engine is free to spread the rows - and therefore the traversals - across fragment
     *   instances. Any number of input rows is accepted, each emitting its own group of output
     *   rows, and a row whose start_node is NULL contributes no output rows. The remaining
     *   arguments are read per row as well, so rows may carry different graphs, end nodes,
     *   constraints and config strings.
     *
     * In both layouts the arguments are read per row, a 'PC<count>' applies per input row, and
     * passing visited_nodes at all rules out 'PC<count>' - regardless of whether any row's
     * visited_nodes is NULL, so that the rejection can never depend on how rows happen to be batched
     * into chunks. visited_nodes is legacy mode only because prefix count mode advances many start
     * nodes' searches concurrently in one shared frontier rather than one start node at a time; the
     * five-argument form of either layout does support 'PC<count>'.
     */
    std::pair<Columns, UInt32Column::Ptr> process(RuntimeState* runtime_state,
                                                  TableFunctionState* state) const override;

    [[nodiscard]] Status close(RuntimeState* runtime_state, TableFunctionState* state) const override {
        delete state;
        return Status::OK();
    }
};

} // namespace starrocks