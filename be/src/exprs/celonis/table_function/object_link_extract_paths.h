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

    struct MyState : public TableFunctionState {
        bool is_initialized = false;
        size_t current_start_node_id = 0;

        std::vector<StackFrame> dfs_stack;
        std::vector<InputCppType> current_path;
        boost::dynamic_bitset<> path_visited;
        boost::dynamic_bitset<> is_end_node;
        LinkPathConfig config;
    };

public:
    [[nodiscard]] Status init(const TFunction& fn, TableFunctionState** state) const override;

    [[nodiscard]] Status prepare(TableFunctionState* state) const override { return Status::OK(); }

    [[nodiscard]] Status open(RuntimeState* runtime_state, TableFunctionState* state) const override {
        return Status::OK();
    }

    /**
     * The process method performs a suspended DFS/backtracking traversal to enumerate paths.
     * Execution yields intermediate results up to the execution engine's chunk size to bound memory usage.
     */
    std::pair<Columns, UInt32Column::Ptr> process(RuntimeState* runtime_state,
                                                  TableFunctionState* state) const override;

    [[nodiscard]] Status close(RuntimeState* runtime_state, TableFunctionState* state) const override {
        delete state;
        return Status::OK();
    }
};

} // namespace starrocks