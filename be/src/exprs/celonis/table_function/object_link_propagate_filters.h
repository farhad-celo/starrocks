#pragma once

#include <boost/dynamic_bitset/dynamic_bitset.hpp>
#include <utility>
#include <vector>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "exprs/table_function/table_function.h"

namespace starrocks {
class CelonisObjectLinkPropagateFilters final : public TableFunction {
    using InputCppType = RunTimeCppType<TYPE_BIGINT>;
    using InputColumnType = RunTimeColumnType<TYPE_BIGINT>;
    using OffsetCppType = RunTimeCppType<TYPE_UNSIGNED_INT>;
    using OffsetColumnType = RunTimeColumnType<TYPE_UNSIGNED_INT>;
    using HopLimitCppType = RunTimeCppType<TYPE_BIGINT>;
    using OutputColumnType = RunTimeColumnType<TYPE_BIGINT>;

    struct MyState : public TableFunctionState {
        boost::dynamic_bitset<> visited;
        std::vector<std::pair<InputCppType, InputCppType>> visited_edges;
        bool is_bfs_done = false;
        size_t current_edge_pos = 0;
    };

public:
    [[nodiscard]] Status init(const TFunction& fn, TableFunctionState** state) const override;

    [[nodiscard]] Status prepare(TableFunctionState* state) const override { return Status::OK(); }

    [[nodiscard]] Status open(RuntimeState* runtime_state, TableFunctionState* state) const override {
        return Status::OK();
    }

    /**
     * The process method in CelonisObjectLinkPropagateFilters performs a Breadth-First Search (BFS) graph traversal to
     * find all traversed edges reachable from a given set of starting nodes, up to an optional maximum depth (hop limit).
     * It takes a graph represented as an adjacency list, traverses it level-by-level, and returns the list
     * of traversed edges (represented by a column of start node IDs and a column of end node IDs).
     */
    std::pair<Columns, UInt32Column::Ptr> process(RuntimeState* runtime_state,
                                                  TableFunctionState* state) const override;

    [[nodiscard]] Status close(RuntimeState* runtime_state, TableFunctionState* state) const override {
        delete state;
        return Status::OK();
    }
};
} // namespace starrocks