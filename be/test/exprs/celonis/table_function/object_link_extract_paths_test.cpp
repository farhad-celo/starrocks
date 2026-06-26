#include "exprs/celonis/table_function/object_link_extract_paths.h"

#include <gtest/gtest.h>

#include "../util.h"
#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "testutil/assert.h"

namespace starrocks {
class CelonisObjectLinkExtractPathsTest : public ::testing::Test {
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
            const std::optional<std::vector<std::optional<RunTimeCppType<LT>>>>& start_vec = std::nullopt,
            const std::optional<std::vector<std::optional<RunTimeCppType<LT>>>>& end_vec = std::nullopt,
            const std::optional<std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>>& constraints =
                    std::nullopt,
            const std::optional<std::string>& config_str = std::nullopt) {
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
        auto final_graph_column = NullableColumn::create(
                ArrayColumn::create(NullableColumn::create(neighbors_column, std::move(inner_nulls)),
                                    std::move(top_offsets)),
                std::move(final_nulls));

        auto create_node_array =
                [&](const std::optional<std::vector<std::optional<RunTimeCppType<LT>>>>& vec) -> ColumnPtr {
            if (!vec.has_value()) {
                auto null_col = NullColumn::create();
                null_col->append(0);

                auto offsets = UInt32Column::create();
                offsets->append(0);
                offsets->append(0);

                auto elements = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());
                return NullableColumn::create(ArrayColumn::create(elements, std::move(offsets)), std::move(null_col));
            }

            auto offsets = UInt32Column::create();
            offsets->append(0);
            auto nodes = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());

            auto current_off = 0U;
            for (const auto& val : vec.value()) {
                if (val.has_value()) {
                    nodes->append_datum(val.value());
                } else {
                    nodes->append_nulls(1);
                }
                current_off++;
            }
            offsets->append(current_off);
            auto nulls = NullColumn::create();
            nulls->append(0);
            return NullableColumn::create(ArrayColumn::create(nodes, offsets), std::move(nulls));
        };

        auto start_nodes_column = create_node_array(start_vec);
        auto end_nodes_column = create_node_array(end_vec);

        ColumnPtr constraints_column;
        if (!constraints.has_value()) {
            // Null constraint column to test handling of NULLs directly
            auto outer_nulls = NullColumn::create();
            outer_nulls->append(1);

            auto inner_elements = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());
            auto inner_offsets = UInt32Column::create();
            auto inner_array = ArrayColumn::create(inner_elements, inner_offsets);

            auto outer_elements_nulls = NullColumn::create(inner_array->size(), 0);
            auto outer_elements = NullableColumn::create(inner_array, std::move(outer_elements_nulls));

            auto outer_offsets = UInt32Column::create();
            outer_offsets->append(0);
            outer_offsets->append(0);

            constraints_column = NullableColumn::create(ArrayColumn::create(outer_elements, std::move(outer_offsets)),
                                                        std::move(outer_nulls));
        } else {
            auto c_neighbors_offsets = UInt32Column::create();
            c_neighbors_offsets->append(0);
            auto c_neighbors = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());
            auto c_current_offset = 0U;

            for (const auto& outer : constraints.value()) {
                for (const auto& elem : outer) {
                    if (elem.has_value()) {
                        c_neighbors->append_datum(elem.value());
                    } else {
                        c_neighbors->append_nulls(1);
                    }
                    c_current_offset++;
                }
                c_neighbors_offsets->append(c_current_offset);
            }

            auto c_neighbors_column = ArrayColumn::create(c_neighbors, c_neighbors_offsets);
            auto c_top_offsets = UInt32Column::create();
            c_top_offsets->append(0);
            c_top_offsets->append(constraints.value().size());
            auto c_inner_nulls = NullColumn::create();
            c_inner_nulls->append_default(constraints.value().size());
            auto c_final_nulls = NullColumn::create();
            c_final_nulls->append(0);
            constraints_column = NullableColumn::create(
                    ArrayColumn::create(NullableColumn::create(c_neighbors_column, std::move(c_inner_nulls)),
                                        std::move(c_top_offsets)),
                    std::move(c_final_nulls));
        }

        Columns input = {final_graph_column, start_nodes_column, end_nodes_column, constraints_column};

        if (config_str.has_value()) {
            auto str_elements = BinaryColumn::create();
            auto str_nulls = NullColumn::create();
            str_elements->append(config_str.value());
            str_nulls->append(0);
            input.push_back(NullableColumn::create(std::move(str_elements), std::move(str_nulls)));
        } else {
            // Null string column to test handling of NULLs directly
            auto str_elements = BinaryColumn::create();
            auto str_nulls = NullColumn::create();
            str_elements->append("");
            str_nulls->append(1);
            input.push_back(NullableColumn::create(std::move(str_elements), std::move(str_nulls)));
        }

        TableFunctionState* table_state;
        auto function = std::make_unique<CelonisObjectLinkExtractPaths>();

        EXPECT_OK(function->init({}, &table_state));
        table_state->set_params(input);
        EXPECT_OK(function->prepare(table_state));

        return {table_state, std::move(function)};
    }

    template <LogicalType LT>
    void EvaluatePaths(const ColumnPtr& result_column,
                       const std::vector<std::vector<RunTimeCppType<LT>>>& expected_paths) {
        auto array_col = down_cast<const ArrayColumn*>(result_column.get());
        auto elements = array_col->elements_column().get();
        auto offsets = array_col->offsets_column();

        ASSERT_EQ(array_col->size(), expected_paths.size()) << "Number of paths mismatch.";

        for (size_t i = 0; i < expected_paths.size(); ++i) {
            size_t start = offsets->get_data()[i];
            size_t end = offsets->get_data()[i + 1];
            ASSERT_EQ(end - start, expected_paths[i].size()) << "Path " << i << " length mismatch.";

            for (size_t j = 0; j < expected_paths[i].size(); ++j) {
                Datum datum = elements->get(start + j);
                EXPECT_EQ(datum.get_int64(), expected_paths[i][j]) << "Mismatch at path " << i << ", element " << j;
            }
        }
    }

    std::unique_ptr<RuntimeState> rt_state_;
};

// Graph Representation:
// 0 ---> 1 ---> 2
TEST_F(CelonisObjectLinkExtractPathsTest, simple_linear) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {}}, {{0L}}, {{2L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 2L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 1 ---> 2
// (No start nodes provided)
TEST_F(CelonisObjectLinkExtractPathsTest, empty_start_nodes) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1}, {2}, {}}, std::vector<std::optional<int64_t>>{}, {{2L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 1 ---> 2 ---> 3
// Start: 1, End: 2
TEST_F(CelonisObjectLinkExtractPathsTest, explicit_start_end) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {3}, {}}, {{1L}}, {{2L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{1L, 2L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 1 -->+
//        |         |
//        0         v
//        |         3
//        +--> 2 -->+
TEST_F(CelonisObjectLinkExtractPathsTest, multiple_paths) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2}, {3}, {3}, {}}, {{0L}}, {{3L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//    +-------------+
//    v             |
//    0 ---> 1 ---> 2
//                  |
//                  v
//                  3
TEST_F(CelonisObjectLinkExtractPathsTest, cyclic_graph) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {0, 3}, {}}, {{0L}}, {{3L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 2L, 3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 1 --+
//        |        |
//        |        v
//        0 --X--> 2   (edge 0->2 is blocked by constraint)
TEST_F(CelonisObjectLinkExtractPathsTest, constraint_edges) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>(
            {{1, 2}, {2}, {}}, {{0L}}, {{2L}}, std::vector<std::vector<std::optional<int64_t>>>{{0L, 1L}, {0L}, {}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 2L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 1       2 ---> 3
// Start: 0, 2
// End: 1, 3
TEST_F(CelonisObjectLinkExtractPathsTest, disconnected_graph_explicit_paths) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {}, {3}, {}}, {{0L, 2L}}, {{1L, 3L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L}, {2L, 3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// [NULL]
TEST_F(CelonisObjectLinkExtractPathsTest, null_in_graph) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{std::nullopt}}, {{0L}}, {{0L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_TRUE(table_state->status().is_invalid_argument());
    function->close(nullptr, table_state);
}

// Graph Representation:
// 0 ---> 1 ---> 2 ---> ... ---> 49
TEST_F(CelonisObjectLinkExtractPathsTest, large_linear_chain) {
    std::vector<std::vector<std::optional<int64_t>>> adj_matrix(50);
    for (int i = 0; i < 49; ++i) {
        adj_matrix[i] = {i + 1};
    }
    adj_matrix[49] = {};

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{49L}}, std::nullopt, "N:LE:50");
    const auto [result_columns, offset] = Run(table_state, function.get());

    std::vector<int64_t> expected_path;
    expected_path.reserve(50);
    for (int64_t i = 0; i < 50; ++i) {
        expected_path.push_back(i);
    }

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {expected_path});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// Binary tree with 50 nodes (0 to 49)
//             0
//           /   \
//          v     v
//         1       2
//        / \     / \
//       v   v   v   v
//       3   4   5   6
//      ...
TEST_F(CelonisObjectLinkExtractPathsTest, large_binary_tree_explicit_ends) {
    std::vector<std::optional<int64_t>> end_nodes;
    for (int64_t i = 25; i <= 49; ++i) end_nodes.push_back(i);

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(
            {{1, 2},   {3, 4},   {5, 6},   {7, 8},   {9, 10},  {11, 12}, {13, 14}, {15, 16}, {17, 18}, {19, 20},
             {21, 22}, {23, 24}, {25, 26}, {27, 28}, {29, 30}, {31, 32}, {33, 34}, {35, 36}, {37, 38}, {39, 40},
             {41, 42}, {43, 44}, {45, 46}, {47, 48}, {49},     {},       {},       {},       {},       {},
             {},       {},       {},       {},       {},       {},       {},       {},       {},       {},
             {},       {},       {},       {},       {},       {},       {},       {},       {},       {}},
            {{0L}}, end_nodes);
    const auto [result_columns, offset] = Run(table_state, function.get());

    std::vector<std::vector<int64_t>> expected_paths;
    std::vector<int64_t> current;
    auto dfs = [&](auto& self, int64_t u) -> void {
        current.push_back(u);
        if (u >= 25) {
            expected_paths.push_back(current);
        }
        if (u < 25) {
            self(self, u * 2 + 1);
            if (u * 2 + 2 <= 49) {
                self(self, u * 2 + 2);
            }
        }
        current.pop_back();
    };

    dfs(dfs, 0);

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], expected_paths);
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// Binary tree with 50 nodes (0 to 49)
// Constraints block edges: 15 -> 31 and 5 -> 11
TEST_F(CelonisObjectLinkExtractPathsTest, large_binary_tree_constraints) {
    std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {
            {1, 2},   {3, 4},   {5, 6},   {7, 8},   {9, 10},  {11, 12}, {13, 14}, {15, 16}, {17, 18}, {19, 20},
            {21, 22}, {23, 24}, {25, 26}, {27, 28}, {29, 30}, {31, 32}, {33, 34}, {35, 36}, {37, 38}, {39, 40},
            {41, 42}, {43, 44}, {45, 46}, {47, 48}, {49},     {},       {},       {},       {},       {},
            {},       {},       {},       {},       {},       {},       {},       {},       {},       {},
            {},       {},       {},       {},       {},       {},       {},       {},       {},       {}};

    std::vector<std::vector<std::optional<int64_t>>> constraints_matrix(adj_matrix.size());
    for (size_t i = 0; i < adj_matrix.size(); ++i) {
        constraints_matrix[i] = std::vector<std::optional<int64_t>>(adj_matrix[i].size(), 0L);
    }
    constraints_matrix[15][0] = 1L;
    constraints_matrix[5][0] = 1L;

    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{31L, 32L, 49L}}, constraints_matrix);
    const auto [result_columns, offset] = Run(table_state, function.get());

    std::vector<std::vector<int64_t>> expected_paths;
    std::vector<int64_t> current;
    auto dfs = [&](auto& self, int64_t u) -> void {
        current.push_back(u);
        if (u == 31 || u == 32 || u == 49) {
            expected_paths.push_back(current);
        }
        if (u < 25) {
            int64_t left = u * 2 + 1;
            if (!(u == 15 && left == 31) && !(u == 5 && left == 11)) {
                self(self, left);
            }
            int64_t right = u * 2 + 2;
            if (right <= 49) {
                if (!(u == 15 && right == 31) && !(u == 5 && right == 11)) {
                    self(self, right);
                }
            }
        }
        current.pop_back();
    };

    dfs(dfs, 0);

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], expected_paths);
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 1 -->+
//        |         |
//        +--> 2 -->+
//        |         |
//      0 +--> 3 -->+ 4
//        |         |
//        +--> 5 -->+
//        |         |
//        +--> 6 -->+
TEST_F(CelonisObjectLinkExtractPathsTest, chunked_output_multiple) {
    rt_state_->set_chunk_size(2);
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1, 2, 3, 5, 6}, {4}, {4}, {4}, {}, {4}, {4}}, {{0L}}, {{4L}});

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 4L}, {0L, 2L, 4L}});
        EXPECT_EQ(offset->size(), 2);
        EXPECT_EQ(offset->get(1).get_uint32(), 2);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 3L, 4L}, {0L, 5L, 4L}});
        EXPECT_EQ(offset->size(), 2);
        EXPECT_EQ(offset->get(1).get_uint32(), 2);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 6L, 4L}});
        EXPECT_EQ(offset->size(), 2);
        EXPECT_EQ(offset->get(1).get_uint32(), 1);
        EXPECT_EQ(table_state->processed_rows(), 1);
    }

    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 1 ---> 3
//        |
//        0
//        |
//        +--> 2 ---> 4
TEST_F(CelonisObjectLinkExtractPathsTest, chunked_output_size_one) {
    rt_state_->set_chunk_size(1);
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2}, {3}, {4}, {}, {}}, {{0L}}, {{3L, 4L}});

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 3L}});
        EXPECT_EQ(offset->get(1).get_uint32(), 1);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 2L, 4L}});
        EXPECT_EQ(offset->get(1).get_uint32(), 1);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(offset->size(), 2);
        EXPECT_EQ(offset->get(1).get_uint32(), 0);
        EXPECT_EQ(table_state->processed_rows(), 1);
    }
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//             +--> 3
//             |
//        +--> 1 ---> 4
//        |
//        0
//        |
//        +--> 2 ---> 5
//             |
//             +--> 6
TEST_F(CelonisObjectLinkExtractPathsTest, chunked_output_deep_backtracking) {
    rt_state_->set_chunk_size(2);
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1, 2}, {3, 4}, {5, 6}, {}, {}, {}, {}}, {{0L}}, {{3L, 4L, 5L, 6L}});

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 3L}, {0L, 1L, 4L}});
        EXPECT_EQ(offset->get(1).get_uint32(), 2);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 2L, 5L}, {0L, 2L, 6L}});
        EXPECT_EQ(offset->get(1).get_uint32(), 2);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(offset->size(), 2);
        EXPECT_EQ(offset->get(1).get_uint32(), 0);
        EXPECT_EQ(table_state->processed_rows(), 1);
    }
    EXPECT_OK(function->close(nullptr, table_state));
}

// Helper graph generator used by length tests.
// Graph Representation:
//        +--> [1..10] -----------------+
//        |                             |
//        |                             v
//        0                         +-> 21
//        |                         |
//        +--> [11..15] -> [16..20] +
//
// (Note: edges between 11..15 and 16..20 are 1-to-1 mappings: 11->16, 12->17, etc.)
std::vector<std::vector<std::optional<int64_t>>> BuildLengthTestGraph() {
    std::vector<std::vector<std::optional<int64_t>>> adj_matrix(22);
    adj_matrix[0] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    for (int i = 1; i <= 10; ++i) {
        adj_matrix[i] = {21};
    }
    for (int i = 11; i <= 15; ++i) {
        adj_matrix[i] = {i + 5};
    }
    for (int i = 16; i <= 20; ++i) {
        adj_matrix[i] = {21};
    }
    adj_matrix[21] = {};
    return adj_matrix;
}

// (See BuildLengthTestGraph for representation)
TEST_F(CelonisObjectLinkExtractPathsTest, length_condition_less_equal_pruning) {
    auto adj_matrix = BuildLengthTestGraph();

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, "N:LE:3");
    const auto [result_columns, offset] = Run(table_state, function.get());

    std::vector<std::vector<int64_t>> expected_paths;
    for (int i = 1; i <= 10; ++i) {
        expected_paths.push_back({0L, static_cast<int64_t>(i), 21L});
    }

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], expected_paths);
    EXPECT_OK(function->close(nullptr, table_state));
}

// (See BuildLengthTestGraph for representation)
TEST_F(CelonisObjectLinkExtractPathsTest, length_condition_equal_pruning) {
    auto adj_matrix = BuildLengthTestGraph();

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, "N:EQ:4");
    const auto [result_columns, offset] = Run(table_state, function.get());

    std::vector<std::vector<int64_t>> expected_paths;
    for (int i = 11; i <= 15; ++i) {
        expected_paths.push_back({0L, static_cast<int64_t>(i), static_cast<int64_t>(i + 5), 21L});
    }

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], expected_paths);
    EXPECT_OK(function->close(nullptr, table_state));
}

// (See BuildLengthTestGraph for representation)
TEST_F(CelonisObjectLinkExtractPathsTest, length_condition_between_pruning) {
    auto adj_matrix = BuildLengthTestGraph();

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, "N:BW:3:4");
    const auto [result_columns, offset] = Run(table_state, function.get());

    std::vector<std::vector<int64_t>> expected_paths;
    for (int i = 1; i <= 10; ++i) {
        expected_paths.push_back({0L, static_cast<int64_t>(i), 21L});
    }
    for (int i = 11; i <= 15; ++i) {
        expected_paths.push_back({0L, static_cast<int64_t>(i), static_cast<int64_t>(i + 5), 21L});
    }

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], expected_paths);
    EXPECT_OK(function->close(nullptr, table_state));
}

// (See BuildLengthTestGraph for representation)
TEST_F(CelonisObjectLinkExtractPathsTest, length_condition_prune_all) {
    auto adj_matrix = BuildLengthTestGraph();

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, "N:LE:2");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 1 ---> 2 ---> ... ---> 10
TEST_F(CelonisObjectLinkExtractPathsTest, explicit_length_less_9) {
    std::vector<std::vector<std::optional<int64_t>>> adj_matrix(11);
    for (int i = 0; i < 10; ++i) {
        adj_matrix[i] = {i + 1};
    }
    adj_matrix[10] = {};

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{10L}}, std::nullopt, "N:LT:10");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 <--> 1 ---> 2
TEST_F(CelonisObjectLinkExtractPathsTest, cyclic_graph_with_allow_cycles) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1}, {0, 2}, {}}, {{0L}}, {{2L}}, std::nullopt, "W:LE:5");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 0L, 1L, 2L}, {0L, 1L, 2L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 (isolated)
//
//         +---+
//         |   v
// 1 ----> 2 --+
//         ^ \
//         |  +----> 4
//         v       /
//         3 -----+
// Note: 2 has edges to 2(self), 3, 4. 3 has edges to 2, 4.
TEST_F(CelonisObjectLinkExtractPathsTest, isolated_and_complex_cycles_combined) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{}, {2}, {2, 3, 4}, {2, 4}, {}}, {{0L, 1L}}, {{0L, 4L}}, std::nullopt, "W:LE:4");

    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L}, {1L, 2L, 2L, 4L}, {1L, 2L, 3L, 4L}, {1L, 2L, 4L}});

    EXPECT_OK(function->close(nullptr, table_state));
}

} // namespace starrocks