#include "exprs/celonis/table_function/object_link_propagate_filters.h"

#include <arrow/testing/gtest_util.h>
#include <gtest/gtest.h>

#include "../util.h"
#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "testutil/assert.h"

namespace starrocks {
class CelonisObjectLinkPropagateFiltersTest : public ::testing::Test {
protected:
    void SetUp() override {
        rt_state_ = std::make_unique<RuntimeState>();
        rt_state_->set_chunk_size(4096);
    }

    void TearDown() override {}

    auto Run(TableFunctionState* table_state, TableFunction* function) {
        return function->process(rt_state_.get(), table_state);
    }

    template <LogicalType LT>
    static std::tuple<TableFunctionState*, std::unique_ptr<TableFunction>> Prepare(
            const std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>& adj_matrix,
            const std::vector<std::optional<RunTimeCppType<LT>>>& start_vec,
            const std::optional<RunTimeCppType<TYPE_BIGINT>> max_hops = std::nullopt) {
        auto neighbors_offsets = UInt32Column::create();
        neighbors_offsets->append(0);

        auto neighbors = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());
        auto current_offset = 0U;

        for (const auto& outer : adj_matrix) {
            for (const auto& elem : outer) {
                if (elem.has_value()) {
                    neighbors->append_datum(elem.value());
                } else {
                    neighbors->append_nulls(1);
                }
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

        auto final_column = NullableColumn::create(
                ArrayColumn::create(NullableColumn::create(neighbors_column, std::move(inner_nulls)),
                                    std::move(top_offsets)),
                std::move(final_nulls));

        auto start_nodes_offsets = UInt32Column::create();
        start_nodes_offsets->append(0);
        auto start_nodes = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());

        current_offset = 0U;
        for (const auto& start : start_vec) {
            if (start.has_value()) {
                start_nodes->append_datum(start.value());
            } else {
                start_nodes->append_nulls(1);
            }
            current_offset++;
        }
        start_nodes_offsets->append(current_offset);

        auto start_nulls = NullColumn::create();
        start_nulls->append(0);

        auto start_nodes_column =
                NullableColumn::create(ArrayColumn::create(start_nodes, start_nodes_offsets), std::move(start_nulls));

        TableFunctionState* table_state;
        auto function = std::make_unique<CelonisObjectLinkPropagateFilters>();
        const auto input = [&]() {
            if (max_hops) {
                auto max_hops_column = RunTimeColumnType<TYPE_BIGINT>::create();
                max_hops_column->append_datum(max_hops.value());
                return Columns{final_column, start_nodes_column, max_hops_column};
            }
            return Columns{final_column, start_nodes_column};
        }();

        EXPECT_OK(function->init({}, &table_state));
        table_state->set_params(input);
        EXPECT_OK(function->prepare(table_state));

        return {table_state, std::move(function)};
    }

    template <LogicalType LT>
    void Evaluate(const ColumnPtr& result_column, const DatumArray& expected) {
        EXPECT_EQ(result_column->size(), expected.size());

        for (size_t row = 0; row < result_column->size(); ++row) {
            EXPECT_EQ(result_column->get(row).get<RunTimeCppType<LT>>(), expected[row].get<RunTimeCppType<LT>>())
                    << "row: " << row;
        }
    }

    std::unique_ptr<RuntimeState> rt_state_;
};

// ┌───┐     ┌───┐     ┌───┐     ┌───┐
// │ 0 │ ──▶ │ 1 │ ──▶ │ 3 │ ──▶ │ 6 │
// └───┘     └───┘     └───┘     └───┘
//   │                             ▲
//   ▼                             │
// ┌───┐     ┌───┐     ┌───┐       │
// │ 2 │ ◀── │ 4 │ ──▶ │ 5 │ ──────┘
// └───┘     └───┘     └───┘
TEST_F(CelonisObjectLinkPropagateFiltersTest, small) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2}, {3}, {}, {6}, {2, 5}, {6}, {}}, {0, 4});
    const auto [result_columns, offset] = Run(table_state, function.get());
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 0L, 4L, 4L, 1L, 5L, 3L}); // Start Nodes
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L, 2L, 2L, 5L, 3L, 6L, 6L}); // End Nodes
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 7});
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkPropagateFiltersTest, small_hop_limit) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2}, {3}, {}, {6}, {2, 5}, {6}, {}}, {0, 4}, 1L);
    const auto [result_columns, offset] = Run(table_state, function.get());
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 0L, 4L, 4L}); // Start Nodes
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L, 2L, 2L, 5L}); // End Nodes
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 4});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Test a simple linear chain: 0 -> 1 -> 2 -> 3
TEST_F(CelonisObjectLinkPropagateFiltersTest, linear) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {3}, {}}, {0});
    const auto [result_columns, offset] = Run(table_state, function.get());
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 1L, 2L}); // Start Nodes
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L, 2L, 3L}); // End Nodes
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 3});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Test a simple linear chain: 0 -> 1 -> 2 -> 3 with a hop limit of 1
TEST_F(CelonisObjectLinkPropagateFiltersTest, linear_hop_limit) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {3}, {}}, {0, 1}, 1L);
    const auto [result_columns, offset] = Run(table_state, function.get());
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 1L}); // Start Nodes
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L, 2L}); // End Nodes
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 2});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Test a graph containing a cycle to ensure propagation doesn't infinite loop: 0 -> 1 -> 2 -> 0
TEST_F(CelonisObjectLinkPropagateFiltersTest, cycle) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {0}}, {0});
    const auto [result_columns, offset] = Run(table_state, function.get());
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 1L, 2L}); // Start Nodes
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L, 2L, 0L}); // End Nodes
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 3});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Test a "star" graph where one node points to many others
TEST_F(CelonisObjectLinkPropagateFiltersTest, star_graph) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2, 3}, {}, {}, {}}, {0});
    const auto [result_columns, offset] = Run(table_state, function.get());
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 0L, 0L}); // Start Nodes
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L, 2L, 3L}); // End Nodes
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 3});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Test starting from a leaf/sink node (has no outgoing edges)
TEST_F(CelonisObjectLinkPropagateFiltersTest, start_at_sink) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2}, {3}, {}, {6}, {2, 5}, {6}, {}}, {6});
    const auto [result_columns, offset] = Run(table_state, function.get());
    // Since node 6 has no neighbors, 0 edges are traversed
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{});
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{});
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 0});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Test an empty graph / empty start nodes
TEST_F(CelonisObjectLinkPropagateFiltersTest, empty) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({}, {});
    const auto [result_columns, offset] = Run(table_state, function.get());
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{});
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{});
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 0});
    EXPECT_OK(function->close(nullptr, table_state));
}

// 50-node binary tree structure. Node i points to 2i+1 and 2i+2.
TEST_F(CelonisObjectLinkPropagateFiltersTest, large_binary_tree) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>(
            {
                    {1, 2},   {3, 4},   {5, 6},   {7, 8},   {9, 10},  // Nodes 0 - 4
                    {11, 12}, {13, 14}, {15, 16}, {17, 18}, {19, 20}, // Nodes 5 - 9
                    {21, 22}, {23, 24}, {25, 26}, {27, 28}, {29, 30}, // Nodes 10 - 14
                    {31, 32}, {33, 34}, {35, 36}, {37, 38}, {39, 40}, // Nodes 15 - 19
                    {41, 42}, {43, 44}, {45, 46}, {47, 48}, {49},     // Nodes 20 - 24
                    {},       {},       {},       {},       {},       // Nodes 25 - 29 (Leaves)
                    {},       {},       {},       {},       {},       // Nodes 30 - 34 (Leaves)
                    {},       {},       {},       {},       {},       // Nodes 35 - 39 (Leaves)
                    {},       {},       {},       {},       {},       // Nodes 40 - 44 (Leaves)
                    {},       {},       {},       {},       {}        // Nodes 45 - 49 (Leaves)
            },
            {0});
    const auto [result_columns, offset] = Run(table_state, function.get());

    Evaluate<TYPE_BIGINT>(
            result_columns[0],
            DatumArray{0L,  0L,  1L,  1L,  2L,  2L,  3L,  3L,  4L,  4L,  5L,  5L,  6L,  6L,  7L,  7L,  8L,
                       8L,  9L,  9L,  10L, 10L, 11L, 11L, 12L, 12L, 13L, 13L, 14L, 14L, 15L, 15L, 16L, 16L,
                       17L, 17L, 18L, 18L, 19L, 19L, 20L, 20L, 21L, 21L, 22L, 22L, 23L, 23L, 24L}); // Start Nodes

    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L,  2L,  3L,  4L,  5L,  6L,  7L,  8L,  9L,  10L, 11L, 12L, 13L,
                                                        14L, 15L, 16L, 17L, 18L, 19L, 20L, 21L, 22L, 23L, 24L, 25L, 26L,
                                                        27L, 28L, 29L, 30L, 31L, 32L, 33L, 34L, 35L, 36L, 37L, 38L, 39L,
                                                        40L, 41L, 42L, 43L, 44L, 45L, 46L, 47L, 48L, 49L}); // End Nodes

    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 49});
    EXPECT_OK(function->close(nullptr, table_state));
}

// 50-node binary tree structure. Node i points to 2i+1 and 2i+2.
TEST_F(CelonisObjectLinkPropagateFiltersTest, large_binary_tree_hop_limit_2) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>(
            {
                    {1, 2},   {3, 4},   {5, 6},   {7, 8},   {9, 10},  // Nodes 0 - 4
                    {11, 12}, {13, 14}, {15, 16}, {17, 18}, {19, 20}, // Nodes 5 - 9
                    {21, 22}, {23, 24}, {25, 26}, {27, 28}, {29, 30}, // Nodes 10 - 14
                    {31, 32}, {33, 34}, {35, 36}, {37, 38}, {39, 40}, // Nodes 15 - 19
                    {41, 42}, {43, 44}, {45, 46}, {47, 48}, {49},     // Nodes 20 - 24
                    {},       {},       {},       {},       {},       // Nodes 25 - 29 (Leaves)
                    {},       {},       {},       {},       {},       // Nodes 30 - 34 (Leaves)
                    {},       {},       {},       {},       {},       // Nodes 35 - 39 (Leaves)
                    {},       {},       {},       {},       {},       // Nodes 40 - 44 (Leaves)
                    {},       {},       {},       {},       {}        // Nodes 45 - 49 (Leaves)
            },
            {0},
            2); // Hop limit of 2
    const auto [result_columns, offset] = Run(table_state, function.get());
    // Hop 0: (0,1), (0,2)
    // Hop 1: (1,3), (1,4), (2,5), (2,6)
    // Limits ends tree expansion here
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 0L, 1L, 1L, 2L, 2L}); // Start Nodes
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{1L, 2L, 3L, 4L, 5L, 6L}); // End Nodes
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 6});
    EXPECT_OK(function->close(nullptr, table_state));
}

// 50-node random graph with disconnected components and cycles.
TEST_F(CelonisObjectLinkPropagateFiltersTest, large_random) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>(
            {
                    {5, 12},  {2}, {1},  {8},          {4}, {19}, {}, {}, {15}, {},   // Nodes 0 - 9
                    {11, 49}, {},  {25}, {},           {},  {22}, {}, {}, {},   {30}, // Nodes 10 - 19
                    {21, 23}, {},  {3},  {},           {},  {30}, {}, {}, {},   {},   // Nodes 20 - 29
                    {42},     {},  {},   {34, 35, 36}, {},  {},   {}, {}, {},   {},   // Nodes 30 - 39
                    {},       {},  {},   {},           {},  {},   {}, {}, {},   {}    // Nodes 40 - 49
            },
            {0, 3}); // Start propagation from nodes 0 and 3
    const auto [result_columns, offset] = Run(table_state, function.get());
    /* * Reachability Trace (Edges):
     * Hop 0: (0, 5), (0, 12), (3, 8)
     * Hop 1: (5, 19), (12, 25), (8, 15)
     * Hop 2: (19, 30), (25, 30), (15, 22)
     * Hop 3: (30, 42), (22, 3)
     */
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 0L, 3L, 5L, 12L, 8L, 19L, 25L, 15L, 30L, 22L});
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{5L, 12L, 8L, 19L, 25L, 15L, 30L, 30L, 22L, 42L, 3L});
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 11});
    EXPECT_OK(function->close(nullptr, table_state));
}

// 50-node random graph with disconnected components and cycles, using a hop limit of 2.
TEST_F(CelonisObjectLinkPropagateFiltersTest, large_random_hop_limit_2) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>(
            {
                    {5, 12},  {2}, {1},  {8},          {4}, {19}, {}, {}, {15}, {},   // Nodes 0 - 9
                    {11, 49}, {},  {25}, {},           {},  {22}, {}, {}, {},   {30}, // Nodes 10 - 19
                    {21, 23}, {},  {3},  {},           {},  {30}, {}, {}, {},   {},   // Nodes 20 - 29
                    {42},     {},  {},   {34, 35, 36}, {},  {},   {}, {}, {},   {},   // Nodes 30 - 39
                    {},       {},  {},   {},           {},  {},   {}, {}, {},   {}    // Nodes 40 - 49
            },
            {0, 3},
            2); // Hop limit of 2
    const auto [result_columns, offset] = Run(table_state, function.get());
    // Cut off after hop 1 expands
    Evaluate<TYPE_BIGINT>(result_columns[0], DatumArray{0L, 0L, 3L, 5L, 12L, 8L});
    Evaluate<TYPE_BIGINT>(result_columns[1], DatumArray{5L, 12L, 8L, 19L, 25L, 15L});
    Evaluate<TYPE_UNSIGNED_INT>(offset, DatumArray{0, 6});
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkPropagateFiltersTest, nulls1) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{0}}, {std::nullopt});
    const auto [result_columns, offset] = Run(table_state, function.get());
    EXPECT_TRUE(table_state->status().is_invalid_argument());
    function->close(nullptr, table_state);
}

TEST_F(CelonisObjectLinkPropagateFiltersTest, nulls2) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{std::nullopt}}, {0});
    const auto [result_columns, offset] = Run(table_state, function.get());
    EXPECT_TRUE(table_state->status().is_invalid_argument());
    function->close(nullptr, table_state);
}

TEST_F(CelonisObjectLinkPropagateFiltersTest, large_random_chunked) {
    // Set a very small chunk size to force multiple process calls
    rt_state_->set_chunk_size(4);

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(
            {
                    {5, 12},  {2}, {1},  {8},          {4}, {19}, {}, {}, {15}, {},   // Nodes 0 - 9
                    {11, 49}, {},  {25}, {},           {},  {22}, {}, {}, {},   {30}, // Nodes 10 - 19
                    {21, 23}, {},  {3},  {},           {},  {30}, {}, {}, {},   {},   // Nodes 20 - 29
                    {42},     {},  {},   {34, 35, 36}, {},  {},   {}, {}, {},   {},   // Nodes 30 - 39
                    {},       {},  {},   {},           {},  {},   {}, {}, {},   {}    // Nodes 40 - 49
            },
            {0, 3});

    // Call 1: Should return first 4 edges
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        Evaluate<TYPE_BIGINT>(results[0], DatumArray{0L, 0L, 3L, 5L});
        Evaluate<TYPE_BIGINT>(results[1], DatumArray{5L, 12L, 8L, 19L});
        EXPECT_EQ(offset->size(), 2);
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 4);
    }

    // Call 2: Should return next 4 edges
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        Evaluate<TYPE_BIGINT>(results[0], DatumArray{12L, 8L, 19L, 25L});
        Evaluate<TYPE_BIGINT>(results[1], DatumArray{25L, 15L, 30L, 30L});
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 4);
    }

    // Call 3: Should return remaining 3 edges and finish
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        Evaluate<TYPE_BIGINT>(results[0], DatumArray{15L, 30L, 22L});
        Evaluate<TYPE_BIGINT>(results[1], DatumArray{22L, 42L, 3L});
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 3);
    }

    EXPECT_OK(function->close(nullptr, table_state));
}
} // namespace starrocks