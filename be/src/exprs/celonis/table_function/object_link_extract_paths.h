#pragma once

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <limits>
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
    };

    struct MyState : public TableFunctionState {
        bool is_initialized = false;
        size_t current_start_node_id = 0;

        std::vector<StackFrame> dfs_stack;
        std::vector<InputCppType> current_path;
        boost::dynamic_bitset<> path_visited;

        bool search_done = false;
        std::vector<std::vector<InputCppType>> frontier;
        std::vector<std::vector<InputCppType>> terminal_paths;
        std::vector<std::vector<InputCppType>> materialized_paths;
        size_t emit_cursor = 0;

        boost::dynamic_bitset<> is_end_node;
        LinkPathConfig config;
    };

    [[nodiscard]] static Status expand_one_hop(const std::vector<std::vector<InputCppType>>& frontier,
                                               const LinkPathConfig& config, const GraphView& graph,
                                               const boost::dynamic_bitset<>& is_end_node,
                                               std::vector<std::vector<InputCppType>>& next_frontier,
                                               std::vector<std::vector<InputCppType>>& terminal);

public:
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
     */
    std::pair<Columns, UInt32Column::Ptr> process(RuntimeState* runtime_state,
                                                  TableFunctionState* state) const override;

    [[nodiscard]] Status close(RuntimeState* runtime_state, TableFunctionState* state) const override {
        delete state;
        return Status::OK();
    }
};

} // namespace starrocks