#pragma once

#include <boost/dynamic_bitset/dynamic_bitset.hpp>

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
        bool is_bfs_done = false;
        boost::dynamic_bitset<>::size_type current_bit_pos = boost::dynamic_bitset<>::npos;
    };

public:
    [[nodiscard]] Status init(const TFunction& fn, TableFunctionState** state) const override;

    [[nodiscard]] Status prepare(TableFunctionState* state) const override { return Status::OK(); }

    [[nodiscard]] Status open(RuntimeState* runtime_state, TableFunctionState* state) const override {
        return Status::OK();
    }

    /**
     * The process method in CelonisObjectLinkPropagateFilters performs a Breadth-First Search (BFS) graph traversal to
     * find all nodes reachable from a given set of starting nodes, up to an optional maximum depth (hop limit).
     * It takes a graph represented as an adjacency list, traverses it level-by-level, and returns a deduplicated list
     * of all visited node IDs.
     */
    std::pair<Columns, UInt32Column::Ptr> process(RuntimeState* runtime_state,
                                                  TableFunctionState* state) const override;

    [[nodiscard]] Status close(RuntimeState* runtime_state, TableFunctionState* state) const override {
        delete state;
        return Status::OK();
    }
};
} // namespace starrocks
