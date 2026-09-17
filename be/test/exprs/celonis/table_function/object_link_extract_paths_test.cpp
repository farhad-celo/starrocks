#include "exprs/celonis/table_function/object_link_extract_paths.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "../util.h"
#include "column/column_helper.h"
#include "column/const_column.h"
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
    static Columns BuildArrayLayoutColumns(
            const std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>& adj_matrix,
            const std::optional<std::vector<std::optional<RunTimeCppType<LT>>>>& start_vec = std::nullopt,
            const std::optional<std::vector<std::optional<RunTimeCppType<LT>>>>& end_vec = std::nullopt,
            const std::optional<std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>>& constraints =
                    std::nullopt,
            const std::optional<std::string>& config_str = std::nullopt,
            const std::optional<std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>>& visited_nodes =
                    std::nullopt,
            const bool pass_visited_nodes = false) {
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
            auto str_elements = BinaryColumn::create();
            auto str_nulls = NullColumn::create();
            str_elements->append("");
            str_nulls->append(1);
            input.push_back(NullableColumn::create(std::move(str_elements), std::move(str_nulls)));
        }

        if (visited_nodes.has_value()) {
            auto v_neighbors_offsets = UInt32Column::create();
            v_neighbors_offsets->append(0);
            auto v_neighbors = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());
            auto v_current_offset = 0U;

            for (const auto& outer : visited_nodes.value()) {
                for (const auto& elem : outer) {
                    if (elem.has_value()) {
                        v_neighbors->append_datum(elem.value());
                    } else {
                        v_neighbors->append_nulls(1);
                    }
                    v_current_offset++;
                }
                v_neighbors_offsets->append(v_current_offset);
            }

            auto v_neighbors_column = ArrayColumn::create(v_neighbors, v_neighbors_offsets);
            auto v_top_offsets = UInt32Column::create();
            v_top_offsets->append(0);
            v_top_offsets->append(visited_nodes.value().size());
            auto v_inner_nulls = NullColumn::create();
            v_inner_nulls->append_default(visited_nodes.value().size());
            auto v_final_nulls = NullColumn::create();
            v_final_nulls->append(0);
            input.push_back(NullableColumn::create(
                    ArrayColumn::create(NullableColumn::create(v_neighbors_column, std::move(v_inner_nulls)),
                                        std::move(v_top_offsets)),
                    std::move(v_final_nulls)));
        } else if (pass_visited_nodes) {
            auto v_final_nulls = NullColumn::create();
            v_final_nulls->append(1);

            auto v_neighbors = NullableColumn::create(RunTimeColumnType<LT>::create(), NullColumn::create());
            auto v_neighbors_column = ArrayColumn::create(std::move(v_neighbors), UInt32Column::create());
            auto v_inner_nulls = NullColumn::create(v_neighbors_column->size(), 0);
            auto v_top_offsets = UInt32Column::create();
            v_top_offsets->append(0);
            v_top_offsets->append(0);

            input.push_back(NullableColumn::create(
                    ArrayColumn::create(NullableColumn::create(std::move(v_neighbors_column), std::move(v_inner_nulls)),
                                        std::move(v_top_offsets)),
                    std::move(v_final_nulls)));
        }

        return input;
    }

    template <LogicalType LT>
    static std::tuple<TableFunctionState*, std::unique_ptr<TableFunction>> Prepare(
            const std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>& adj_matrix,
            const std::optional<std::vector<std::optional<RunTimeCppType<LT>>>>& start_vec = std::nullopt,
            const std::optional<std::vector<std::optional<RunTimeCppType<LT>>>>& end_vec = std::nullopt,
            const std::optional<std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>>& constraints =
                    std::nullopt,
            const std::optional<std::string>& config_str = std::nullopt,
            const std::optional<std::vector<std::vector<std::optional<RunTimeCppType<LT>>>>>& visited_nodes =
                    std::nullopt,
            const bool expanded_output = true) {
        return BuildArrayLayoutState(
                BuildArrayLayoutColumns<LT>(adj_matrix, start_vec, end_vec, constraints, config_str, visited_nodes),
                expanded_output);
    }

    struct ArrayLayoutRow {
        std::vector<std::vector<std::optional<int64_t>>> adj_matrix;
        std::optional<std::vector<std::optional<int64_t>>> start_nodes = std::nullopt;
        std::optional<std::vector<std::optional<int64_t>>> end_nodes = std::nullopt;
        std::optional<std::vector<std::vector<std::optional<int64_t>>>> constraints = std::nullopt;
        std::optional<std::string> config_str = std::nullopt;
        std::optional<std::vector<std::vector<std::optional<int64_t>>>> visited_nodes = std::nullopt;
    };

    static std::tuple<TableFunctionState*, std::unique_ptr<TableFunction>> PrepareArrayRows(
            const std::vector<ArrayLayoutRow>& rows, const bool pass_visited_nodes = false) {
        const bool with_visited_nodes =
                pass_visited_nodes || std::any_of(rows.begin(), rows.end(), [](const ArrayLayoutRow& row) {
                    return row.visited_nodes.has_value();
                });

        std::vector<MutableColumnPtr> merged;
        for (const auto& row : rows) {
            Columns row_columns = BuildArrayLayoutColumns<TYPE_BIGINT>(row.adj_matrix, row.start_nodes, row.end_nodes,
                                                                       row.constraints, row.config_str,
                                                                       row.visited_nodes, with_visited_nodes);
            if (merged.empty()) {
                for (const auto& column : row_columns) {
                    merged.push_back(column->clone());
                }
                continue;
            }
            EXPECT_EQ(merged.size(), row_columns.size()) << "Every row has to pass the same arguments";
            for (size_t i = 0; i < row_columns.size(); i++) {
                merged[i]->append(*row_columns[i], 0, 1);
            }
        }

        Columns input;
        input.reserve(merged.size());
        for (auto& column : merged) {
            input.emplace_back(std::move(column));
        }
        return BuildArrayLayoutState(std::move(input));
    }

    static std::tuple<TableFunctionState*, std::unique_ptr<TableFunction>> BuildArrayLayoutState(
            Columns input, const bool expanded_output = true) {
        TableFunctionState* table_state;
        auto function = std::make_unique<CelonisObjectLinkExtractPaths>(/*row_per_start_node=*/false, expanded_output);

        EXPECT_OK(function->init({}, &table_state));
        table_state->set_params(std::move(input));
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

    template <LogicalType LT>
    static std::vector<std::vector<RunTimeCppType<LT>>> CollectPaths(const ColumnPtr& result_column) {
        auto array_col = down_cast<const ArrayColumn*>(result_column.get());
        auto elements = array_col->elements_column().get();
        auto offsets = array_col->offsets_column();

        std::vector<std::vector<RunTimeCppType<LT>>> paths;
        paths.reserve(array_col->size());
        for (size_t i = 0; i < array_col->size(); ++i) {
            std::vector<RunTimeCppType<LT>> path;
            for (size_t j = offsets->get_data()[i]; j < offsets->get_data()[i + 1]; ++j) {
                path.push_back(elements->get(j).get<RunTimeCppType<LT>>());
            }
            paths.push_back(std::move(path));
        }
        return paths;
    }

    static std::vector<int64_t> CollectPathLengths(const ColumnPtr& result_column) {
        auto* col = down_cast<const Int64Column*>(result_column.get());
        return {col->get_data().begin(), col->get_data().end()};
    }

    static std::vector<std::optional<int64_t>> CollectResumeNodes(const ColumnPtr& result_column) {
        auto* nullable_col = down_cast<const NullableColumn*>(result_column.get());
        auto* data_col = down_cast<const Int64Column*>(nullable_col->data_column().get());
        std::vector<std::optional<int64_t>> result;
        result.reserve(nullable_col->size());
        for (size_t i = 0; i < nullable_col->size(); i++) {
            if (nullable_col->is_null(i)) {
                result.push_back(std::nullopt);
            } else {
                result.push_back(data_col->get_data()[i]);
            }
        }
        return result;
    }

    struct StartNodeRow {
        std::vector<std::vector<std::optional<int64_t>>> adj_matrix;
        std::optional<int64_t> start_node;
        std::vector<std::optional<int64_t>> end_nodes;
        std::optional<std::vector<std::vector<std::optional<int64_t>>>> constraints = std::nullopt;
        std::optional<std::vector<std::optional<int64_t>>> visited_nodes = std::nullopt;
    };

    static std::tuple<TableFunctionState*, std::unique_ptr<TableFunction>> PrepareRows(
            const std::vector<StartNodeRow>& rows, const std::optional<std::string>& config_str = std::nullopt,
            const bool pass_visited_nodes = false) {
        auto build_matrix_column = [&rows](auto&& get) -> ColumnPtr {
            auto adjacency_offsets = UInt32Column::create();
            adjacency_offsets->append(0);
            auto neighbors = NullableColumn::create(Int64Column::create(), NullColumn::create());
            auto row_offsets = UInt32Column::create();
            row_offsets->append(0);
            auto row_nulls = NullColumn::create();

            uint32_t neighbor_count = 0;
            uint32_t adjacency_count = 0;
            for (const auto& row : rows) {
                const auto* matrix = get(row);
                if (matrix == nullptr) {
                    row_nulls->append(1);
                } else {
                    for (const auto& adjacency : *matrix) {
                        for (const auto& node_id : adjacency) {
                            if (node_id.has_value()) {
                                neighbors->append_datum(node_id.value());
                            } else {
                                neighbors->append_nulls(1);
                            }
                            neighbor_count++;
                        }
                        adjacency_offsets->append(neighbor_count);
                        adjacency_count++;
                    }
                    row_nulls->append(0);
                }
                row_offsets->append(adjacency_count);
            }

            auto adjacency_lists = ArrayColumn::create(std::move(neighbors), std::move(adjacency_offsets));
            auto adjacency_nulls = NullColumn::create(adjacency_count, 0);
            return NullableColumn::create(
                    ArrayColumn::create(NullableColumn::create(std::move(adjacency_lists), std::move(adjacency_nulls)),
                                        std::move(row_offsets)),
                    std::move(row_nulls));
        };

        auto build_array_column = [&rows](auto&& get) -> ColumnPtr {
            auto offsets = UInt32Column::create();
            offsets->append(0);
            auto elements = NullableColumn::create(Int64Column::create(), NullColumn::create());
            auto nulls = NullColumn::create();

            uint32_t element_count = 0;
            for (const auto& row : rows) {
                const auto* values = get(row);
                if (values == nullptr) {
                    nulls->append(1);
                } else {
                    for (const auto& value : *values) {
                        if (value.has_value()) {
                            elements->append_datum(value.value());
                        } else {
                            elements->append_nulls(1);
                        }
                        element_count++;
                    }
                    nulls->append(0);
                }
                offsets->append(element_count);
            }
            return NullableColumn::create(ArrayColumn::create(std::move(elements), std::move(offsets)),
                                          std::move(nulls));
        };

        auto start_nodes = NullableColumn::create(Int64Column::create(), NullColumn::create());
        auto config_elements = BinaryColumn::create();
        auto config_nulls = NullColumn::create();
        for (const auto& row : rows) {
            if (row.start_node.has_value()) {
                start_nodes->append_datum(row.start_node.value());
            } else {
                start_nodes->append_nulls(1);
            }
            config_elements->append(config_str.value_or(""));
            config_nulls->append(config_str.has_value() ? 0 : 1);
        }

        Columns input = {build_matrix_column([](const StartNodeRow& row) { return &row.adj_matrix; }),
                         std::move(start_nodes),
                         build_array_column([](const StartNodeRow& row) { return &row.end_nodes; }),
                         build_matrix_column([](const StartNodeRow& row) {
                             return row.constraints.has_value() ? &row.constraints.value() : nullptr;
                         }),
                         NullableColumn::create(std::move(config_elements), std::move(config_nulls))};
        if (pass_visited_nodes) {
            input.push_back(build_array_column([](const StartNodeRow& row) {
                return row.visited_nodes.has_value() ? &row.visited_nodes.value() : nullptr;
            }));
        }

        TableFunctionState* table_state;
        auto function = std::make_unique<CelonisObjectLinkExtractPaths>(/*row_per_start_node=*/true);

        EXPECT_OK(function->init({}, &table_state));
        table_state->set_params(input);
        EXPECT_OK(function->prepare(table_state));

        return {table_state, std::move(function)};
    }

    static std::vector<uint32_t> CollectRowOffsets(const UInt32Column::Ptr& offsets) {
        return {offsets->get_data().begin(), offsets->get_data().end()};
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

TEST_F(CelonisObjectLinkExtractPathsTest, path_length_and_resume_node_legacy_mode) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2}, {3}, {3}, {}}, {{0L}}, {{3L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}});
    EXPECT_EQ(CollectPathLengths(result_columns[1]), std::vector<int64_t>({3, 3}));
    EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({std::nullopt, std::nullopt}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, legacy_contract_returns_only_path_column) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1, 2}, {3}, {3}, {}}, {{0L}}, {{3L}}, std::nullopt, std::nullopt, std::nullopt,
                                 /*expanded_output=*/false);
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EXPECT_EQ(result_columns.size(), 1);
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, legacy_contract_supports_prefix_count) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1, 2}, {3}, {3}, {}}, {{0L}}, {{3L}}, std::nullopt,
                                                              "N:PC2", std::nullopt, /*expanded_output=*/false);
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EXPECT_EQ(result_columns.size(), 1);
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L}, {0L, 2L}});
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

TEST_F(CelonisObjectLinkExtractPathsTest, path_length_and_resume_node_chunked_output) {
    rt_state_->set_chunk_size(2);
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1, 2, 3, 5, 6}, {4}, {4}, {4}, {}, {4}, {4}}, {{0L}}, {{4L}});

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 4L}, {0L, 2L, 4L}});
        EXPECT_EQ(CollectPathLengths(results[1]), std::vector<int64_t>({3, 3}));
        EXPECT_EQ(CollectResumeNodes(results[2]), std::vector<std::optional<int64_t>>({std::nullopt, std::nullopt}));
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 3L, 4L}, {0L, 5L, 4L}});
        EXPECT_EQ(CollectPathLengths(results[1]), std::vector<int64_t>({3, 3}));
        EXPECT_EQ(CollectResumeNodes(results[2]), std::vector<std::optional<int64_t>>({std::nullopt, std::nullopt}));
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 6L, 4L}});
        EXPECT_EQ(CollectPathLengths(results[1]), std::vector<int64_t>({3}));
        EXPECT_EQ(CollectResumeNodes(results[2]), std::vector<std::optional<int64_t>>({std::nullopt}));
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

TEST_F(CelonisObjectLinkExtractPathsTest, length_condition_less_equal_legacy_trailing_zero) {
    auto adj_matrix = BuildLengthTestGraph();

    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, "N:LE:3:0");
    const auto [result_columns, offset] = Run(table_state, function.get());

    std::vector<std::vector<int64_t>> expected_paths;
    for (int i = 1; i <= 10; ++i) {
        expected_paths.push_back({0L, static_cast<int64_t>(i), 21L});
    }

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], expected_paths);
    EXPECT_OK(function->close(nullptr, table_state));
}

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

// Graph Representation:
//                     +--> 4
//                     |
//              +--> 2 +
//              |      |
//              |      +--> 5
// 0 ---> 1(end)+
//              |      +--> 6
//              |      |
//              +--> 3 +
//                     |
//                     +--> 7
std::vector<std::vector<std::optional<int64_t>>> BuildPrefixCountTestGraph() {
    return {{1}, {2, 3}, {4, 5}, {6, 7}, {}, {}, {}, {}};
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_grows_frontier_until_satisfied) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>(BuildPrefixCountTestGraph(), {{0L}}, {{1L}}, std::nullopt, "N:PC4");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0],
                               {{0L, 1L}, {0L, 1L, 2L, 4L}, {0L, 1L, 2L, 5L}, {0L, 1L, 3L, 6L}, {0L, 1L, 3L, 7L}});
    EXPECT_EQ(table_state->processed_rows(), 1);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_marks_growing_prefixes_incomplete) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>(BuildPrefixCountTestGraph(), {{0L}}, {{1L}}, std::nullopt, "N:PC4");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0],
                               {{0L, 1L}, {0L, 1L, 2L, 4L}, {0L, 1L, 2L, 5L}, {0L, 1L, 3L, 6L}, {0L, 1L, 3L, 7L}});
    EXPECT_EQ(CollectPathLengths(result_columns[1]), std::vector<int64_t>({2, 4, 4, 4, 4}));
    EXPECT_EQ(CollectResumeNodes(result_columns[2]),
              std::vector<std::optional<int64_t>>({std::nullopt, 4L, 5L, 6L, 7L}));
    EXPECT_EQ(table_state->processed_rows(), 1);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_chunked_output) {
    rt_state_->set_chunk_size(2);
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>(BuildPrefixCountTestGraph(), {{0L}}, {{1L}}, std::nullopt, "N:PC4");

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L}, {0L, 1L, 2L, 4L}});
        EXPECT_EQ(offset->get(1).get_uint32(), 2);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 2L, 5L}, {0L, 1L, 3L, 6L}});
        EXPECT_EQ(offset->get(1).get_uint32(), 2);
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 3L, 7L}});
        EXPECT_EQ(offset->get(1).get_uint32(), 1);
        EXPECT_EQ(table_state->processed_rows(), 1);
    }
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_resume_node_survives_chunk_boundaries) {
    rt_state_->set_chunk_size(2);
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>(BuildPrefixCountTestGraph(), {{0L}}, {{1L}}, std::nullopt, "N:PC4");

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L}, {0L, 1L, 2L, 4L}});
        EXPECT_EQ(CollectPathLengths(results[1]), std::vector<int64_t>({2, 4}));
        EXPECT_EQ(CollectResumeNodes(results[2]), std::vector<std::optional<int64_t>>({std::nullopt, 4L}));
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 2L, 5L}, {0L, 1L, 3L, 6L}});
        EXPECT_EQ(CollectPathLengths(results[1]), std::vector<int64_t>({4, 4}));
        EXPECT_EQ(CollectResumeNodes(results[2]), std::vector<std::optional<int64_t>>({5L, 6L}));
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 3L, 7L}});
        EXPECT_EQ(CollectPathLengths(results[1]), std::vector<int64_t>({4}));
        EXPECT_EQ(CollectResumeNodes(results[2]), std::vector<std::optional<int64_t>>({7L}));
    }
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_chunked_output_across_rounds) {
    rt_state_->set_chunk_size(4);
    auto adj_matrix = BuildLengthTestGraph();
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, "N:BW:3:4:PC1000");

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 1L, 21L}, {0L, 2L, 21L}, {0L, 3L, 21L}, {0L, 4L, 21L}});
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 5L, 21L}, {0L, 6L, 21L}, {0L, 7L, 21L}, {0L, 8L, 21L}});
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0],
                                   {{0L, 9L, 21L}, {0L, 10L, 21L}, {0L, 11L, 16L, 21L}, {0L, 12L, 17L, 21L}});
        EXPECT_EQ(table_state->processed_rows(), 0);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EvaluatePaths<TYPE_BIGINT>(results[0], {{0L, 13L, 18L, 21L}, {0L, 14L, 19L, 21L}, {0L, 15L, 20L, 21L}});
        EXPECT_EQ(table_state->processed_rows(), 1);
    }
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 (end, isolated)      1 ---> 2      3 (end, isolated)
TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_satisfied_by_start_nodes) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{}, {2}, {}, {}}, {{0L, 3L}}, {{0L, 3L}}, std::nullopt, "N:PC2");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L}, {3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_satisfied_by_start_nodes_marks_rows_complete) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{}, {2}, {}, {}}, {{0L, 3L}}, {{0L, 3L}}, std::nullopt, "N:PC2");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L}, {3L}});
    EXPECT_EQ(CollectPathLengths(result_columns[1]), std::vector<int64_t>({1, 1}));
    EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({std::nullopt, std::nullopt}));
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 1 ---> 4(end)      2 ---> 3 ---> 4(end)
TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_always_advances_at_least_one_hop) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1}, {4}, {3}, {4}, {}}, {{0L, 2L}}, {{4L}}, std::nullopt, "N:PC2");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L}, {2L, 3L}});
    EXPECT_EQ(table_state->processed_rows(), 1);
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//    +-------+
//    v       |
//    0 ----> 1
// (no end nodes at all)
TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_search_honors_cancellation) {
    rt_state_->set_is_cancelled(true);
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1}, {0}}, {{0L}}, std::vector<std::optional<int64_t>>{}, std::nullopt, "W:PC5");
    Run(table_state, function.get());

    EXPECT_TRUE(table_state->status().is_cancelled()) << table_state->status().to_string();
    EXPECT_EQ(table_state->processed_rows(), 0);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_exhausted_matches_legacy_mode) {
    auto adj_matrix = BuildLengthTestGraph();

    auto sorted_paths_for = [&](const std::optional<std::string>& config_str) {
        const auto [table_state, function] =
                Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, config_str);
        const auto [result_columns, offset] = Run(table_state, function.get());
        EXPECT_OK(table_state->status());
        EXPECT_EQ(table_state->processed_rows(), 1);
        auto paths = CollectPaths<TYPE_BIGINT>(result_columns[0]);
        EXPECT_OK(function->close(nullptr, table_state));
        std::sort(paths.begin(), paths.end());
        return paths;
    };

    const auto prefix_mode_paths = sorted_paths_for("N:BW:3:4:PC1000");
    const auto legacy_mode_paths = sorted_paths_for("N:BW:3:4");

    EXPECT_FALSE(legacy_mode_paths.empty());
    EXPECT_EQ(prefix_mode_paths, legacy_mode_paths);
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_exhausted_matches_legacy_mode_metadata) {
    using Row = std::tuple<std::vector<int64_t>, int64_t, std::optional<int64_t>>;
    auto adj_matrix = BuildLengthTestGraph();

    auto sorted_rows_for = [&](const std::optional<std::string>& config_str) {
        const auto [table_state, function] =
                Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{21L}}, std::nullopt, config_str);
        const auto [result_columns, offset] = Run(table_state, function.get());
        EXPECT_OK(table_state->status());
        EXPECT_EQ(table_state->processed_rows(), 1);
        auto paths = CollectPaths<TYPE_BIGINT>(result_columns[0]);
        auto lengths = CollectPathLengths(result_columns[1]);
        auto resume_nodes = CollectResumeNodes(result_columns[2]);
        EXPECT_OK(function->close(nullptr, table_state));

        std::vector<Row> rows;
        for (size_t i = 0; i < paths.size(); ++i) {
            rows.emplace_back(paths[i], lengths[i], resume_nodes[i]);
        }
        std::sort(rows.begin(), rows.end());
        return rows;
    };

    const auto prefix_mode_rows = sorted_rows_for("N:BW:3:4:PC1000");
    const auto legacy_mode_rows = sorted_rows_for("N:BW:3:4");

    EXPECT_FALSE(legacy_mode_rows.empty());
    EXPECT_EQ(prefix_mode_rows, legacy_mode_rows);
    for (const auto& [path, length, resume_node] : legacy_mode_rows) {
        EXPECT_EQ(resume_node, std::nullopt);
        EXPECT_EQ(length, static_cast<int64_t>(path.size()));
    }
}

// Graph Representation:
//    +-------+
//    v       |
//    0 ----> 1
// (no end nodes at all)
TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_unreachable_end_hits_safety_cap) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1}, {0}}, {{0L}}, std::vector<std::optional<int64_t>>{}, std::nullopt, "W:PC5");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 0L, 1L}});
    EXPECT_EQ(table_state->processed_rows(), 1);
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 <--> 1 ---> 2
TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_allows_cycles_without_upper_bound) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {0, 2}, {}}, {{0L}}, {{2L}}, std::nullopt, "W:PC3");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 2L}, {0L, 1L, 0L, 1L, 0L}, {0L, 1L, 0L, 1L, 2L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 --+
//     +--> 2 ---> 3
// 1 --+
TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_cycle_detection_is_per_path) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{2}, {2}, {3}, {}}, {{0L, 1L}}, {{3L}}, std::nullopt, "N:PC100");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 2L, 3L}, {1L, 2L, 3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//    +-------+
//    v       |
//    0 ----> 1 (end)
TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_upper_bound_above_node_count) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1}, {0}}, {{0L}}, {{1L}}, std::nullopt, "W:LE:9:PC1000");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(
            result_columns[0],
            {{0L, 1L}, {0L, 1L, 0L, 1L}, {0L, 1L, 0L, 1L, 0L, 1L}, {0L, 1L, 0L, 1L, 0L, 1L, 0L, 1L}});
    EXPECT_EQ(table_state->processed_rows(), 1);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_token_order_is_irrelevant) {
    auto paths_for = [&](const std::string& config_str) {
        const auto [table_state, function] =
                Prepare<TYPE_BIGINT>(BuildPrefixCountTestGraph(), {{0L}}, {{1L}}, std::nullopt, config_str);
        const auto [result_columns, offset] = Run(table_state, function.get());
        EXPECT_TRUE(table_state->status().ok()) << "Config '" << config_str << "' was rejected";
        auto paths = CollectPaths<TYPE_BIGINT>(result_columns[0]);
        EXPECT_OK(function->close(nullptr, table_state));
        return paths;
    };

    const auto count_first_paths = paths_for("N:PC4:BW:2:4");
    EXPECT_FALSE(count_first_paths.empty());
    EXPECT_EQ(count_first_paths, paths_for("N:BW:2:4:PC4"));
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_malformed_config) {
    const std::vector<std::string> invalid_configs = {
            "N:PC",      "N:PC0",    "N:PC-1",     "N:PCx",    "N:LE:3:PC0", "N:LE:3:PC-1",    "N:LE:3:PCx",
            "N:LE:3:PC", "N:LE:3:5", "N:BW:3:4:5", "N:PC5:LE", "N:PC5:BW:3", "N:LE:3:PC5:PC5", "N:LE:3:GE:1"};

    for (const auto& config_str : invalid_configs) {
        const auto [table_state, function] =
                Prepare<TYPE_BIGINT>({{1}, {2}, {}}, {{0L}}, {{2L}}, std::nullopt, config_str);
        Run(table_state, function.get());

        EXPECT_TRUE(table_state->status().is_invalid_argument()) << "Config '" << config_str << "' was accepted";
        EXPECT_OK(function->close(nullptr, table_state));
    }
}

// Graph Representation:
//        +--> 1 --+
//        |        |
//        +--(0)   v
// 2 <-------+      3
//                  ^
//                  |
//        +---------+
// i.e. 0->1, 1->{2,3}, 2->0, 3->{} ; end node 3.
TEST_F(CelonisObjectLinkExtractPathsTest, visited_nodes_blocks_reentry) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1}, {2, 3}, {0}, {}};

    {
        const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{2L}}, {{3L}});
        const auto [result_columns, offset] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{2L, 0L, 1L, 3L}});
        EXPECT_OK(function->close(nullptr, table_state));
    }
    {
        const auto [table_state, function] =
                Prepare<TYPE_BIGINT>(adj_matrix, {{2L}}, {{3L}}, std::nullopt, std::nullopt,
                                     std::vector<std::vector<std::optional<int64_t>>>{{0L, 1L, 2L}});
        const auto [result_columns, offset] = Run(table_state, function.get());
        EXPECT_OK(table_state->status());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {});
        EXPECT_OK(function->close(nullptr, table_state));
    }
}

TEST_F(CelonisObjectLinkExtractPathsTest, visited_nodes_ignored_when_cycles_allowed) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1}, {2, 3}, {0}, {}};

    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>(adj_matrix, {{2L}}, {{3L}}, std::nullopt, "W:LE:4",
                                 std::vector<std::vector<std::optional<int64_t>>>{{0L, 1L, 2L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{2L, 0L, 1L, 3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, visited_nodes_length_mismatch_rejected) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {}}, {{0L, 1L}}, {{1L}}, std::nullopt, std::nullopt,
                                                              std::vector<std::vector<std::optional<int64_t>>>{{0L}});
    Run(table_state, function.get());

    EXPECT_TRUE(table_state->status().is_invalid_argument());
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, visited_nodes_omitted_matches_legacy_five_arg_call) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {}}, {{0L}}, {{2L}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 2L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, visited_nodes_rejected_with_prefix_count) {
    const auto [table_state, function] = Prepare<TYPE_BIGINT>({{1}, {2}, {}}, {{0L}}, {{2L}}, std::nullopt, "N:PC2",
                                                              std::vector<std::vector<std::optional<int64_t>>>{{0L}});
    Run(table_state, function.get());

    EXPECT_TRUE(table_state->status().is_invalid_argument());
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 2 ---> 3 <--- 2 <--- 1
// (0 and 1 both point at 2, which points at 3)
TEST_F(CelonisObjectLinkExtractPathsTest, visited_nodes_is_independent_per_start_node) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{2}, {2}, {3}, {}}, {{0L, 1L}}, {{3L}}, std::nullopt, std::nullopt,
                                 std::vector<std::vector<std::optional<int64_t>>>{{2L}, {}});
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{1L, 2L, 3L}});
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, resume_node_handover_across_two_calls) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1}, {2, 3}, {0}, {4}, {}};

    const auto [first_state, first_function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{4L}}, std::nullopt, "N:PC2");
    const auto [first_columns, first_offset] = Run(first_state, first_function.get());
    const auto first_paths = CollectPaths<TYPE_BIGINT>(first_columns[0]);
    const auto resume_nodes = CollectResumeNodes(first_columns[2]);
    ASSERT_EQ(first_paths, (std::vector<std::vector<int64_t>>{{0L, 1L, 2L}, {0L, 1L, 3L}}));
    ASSERT_EQ(resume_nodes, (std::vector<std::optional<int64_t>>{2L, 3L}));
    EXPECT_OK(first_function->close(nullptr, first_state));

    const std::vector<std::optional<int64_t>> next_start_nodes = resume_nodes;
    const std::vector<std::vector<std::optional<int64_t>>> visited_nodes_for_next_call = {
            std::vector<std::optional<int64_t>>(first_paths[0].begin(), first_paths[0].end()),
            std::vector<std::optional<int64_t>>(first_paths[1].begin(), first_paths[1].end())};

    {
        const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, next_start_nodes, {{4L}});
        const auto [result_columns, offset] = Run(table_state, function.get());
        EXPECT_OK(table_state->status());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{2L, 0L, 1L, 3L, 4L}, {3L, 4L}});
        EXPECT_OK(function->close(nullptr, table_state));
    }
    {
        const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, next_start_nodes, {{4L}}, std::nullopt,
                                                                  std::nullopt, visited_nodes_for_next_call);
        const auto [result_columns, offset] = Run(table_state, function.get());
        EXPECT_OK(table_state->status());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{3L, 4L}});
        EXPECT_OK(function->close(nullptr, table_state));
    }
}

TEST_F(CelonisObjectLinkExtractPathsTest, prefix_count_resume_node_matches_own_path_across_start_nodes) {
    const auto [table_state, function] =
            Prepare<TYPE_BIGINT>({{1}, {2, 3}, {}, {}, {5}, {}}, {{0L, 4L}}, {{5L}}, std::nullopt, "N:PC3");
    const auto [result_columns, offset] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{4L, 5L}, {0L, 1L, 2L}, {0L, 1L, 3L}});
    EXPECT_EQ(CollectPathLengths(result_columns[1]), std::vector<int64_t>({2, 3, 3}));
    EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({std::nullopt, 2L, 3L}));
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 1 --+
//        |        v
//        0        3
//        |        ^
//        +--> 2 --+
TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_groups_output_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] = PrepareRows({{.adj_matrix = adj_matrix, .start_node = 0L, .end_nodes = {3L}},
                                                      {.adj_matrix = adj_matrix, .start_node = 1L, .end_nodes = {3L}}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}, {1L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 3}));
    EXPECT_EQ(table_state->processed_rows(), 2);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_null_start_node_emits_nothing) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] =
            PrepareRows({{.adj_matrix = adj_matrix, .start_node = 0L, .end_nodes = {3L}},
                         {.adj_matrix = adj_matrix, .start_node = std::nullopt, .end_nodes = {3L}},
                         {.adj_matrix = adj_matrix, .start_node = 1L, .end_nodes = {3L}}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}, {1L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 2, 3}));
    EXPECT_EQ(table_state->processed_rows(), 3);
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 2 ---> 3 <--- 2 <--- 1
// (0 and 1 both point at 2, which points at 3)
TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_visited_nodes_are_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{2}, {2}, {3}, {}};
    const auto [table_state, function] = PrepareRows(
            {{.adj_matrix = adj_matrix, .start_node = 0L, .end_nodes = {3L}, .visited_nodes = {{2L}}},
             {.adj_matrix = adj_matrix, .start_node = 1L, .end_nodes = {3L}, .visited_nodes = std::nullopt}},
            std::nullopt, /*pass_visited_nodes=*/true);
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{1L, 2L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 0, 1}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_reads_graph_and_end_nodes_per_input_row) {
    const auto [table_state, function] =
            PrepareRows({{.adj_matrix = {{1}, {}}, .start_node = 0L, .end_nodes = {1L}},
                         {.adj_matrix = {{1}, {2}, {}}, .start_node = 0L, .end_nodes = {2L}}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L}, {0L, 1L, 2L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1, 2}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_resumes_across_chunks) {
    rt_state_->set_chunk_size(1);

    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] = PrepareRows({{.adj_matrix = adj_matrix, .start_node = 0L, .end_nodes = {3L}},
                                                      {.adj_matrix = adj_matrix, .start_node = 1L, .end_nodes = {3L}}});

    std::vector<std::vector<int64_t>> paths;
    size_t calls = 0;
    while (table_state->processed_rows() < 2) {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        ASSERT_OK(table_state->status());
        const auto row_offsets = CollectRowOffsets(offsets);
        ASSERT_FALSE(row_offsets.empty());
        EXPECT_EQ(row_offsets.front(), 0);
        EXPECT_LE(row_offsets.back(), 1);
        for (auto& path : CollectPaths<TYPE_BIGINT>(result_columns[0])) {
            paths.push_back(std::move(path));
        }
        ASSERT_LT(++calls, 10) << "process() is not making progress";
    }

    EXPECT_EQ(paths, (std::vector<std::vector<int64_t>>{{0L, 1L, 3L}, {0L, 2L, 3L}, {1L, 3L}}));
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 1 ---> {2, 3}
TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_prefix_count_applies_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1}, {2, 3}, {}, {}};
    const auto [table_state, function] = PrepareRows({{.adj_matrix = adj_matrix, .start_node = 0L, .end_nodes = {}},
                                                      {.adj_matrix = adj_matrix, .start_node = 1L, .end_nodes = {}}},
                                                     "N:PC2");
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 2L}, {0L, 1L, 3L}, {1L, 2L}, {1L, 3L}});
    EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({2L, 3L, 2L, 3L}));
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 4}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_visited_nodes_rejected_with_prefix_count) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1}, {2}, {}};

    const std::vector<std::optional<std::vector<std::optional<int64_t>>>> visited_node_variants = {
            std::vector<std::optional<int64_t>>{0L}, std::nullopt};
    for (const auto& visited_nodes : visited_node_variants) {
        const auto [table_state, function] = PrepareRows(
                {{.adj_matrix = adj_matrix, .start_node = 1L, .end_nodes = {2L}, .visited_nodes = visited_nodes}},
                "N:PC2", /*pass_visited_nodes=*/true);
        Run(table_state, function.get());

        EXPECT_TRUE(table_state->status().is_invalid_argument())
                << "A prefix count was accepted alongside the visited_nodes argument";
        EXPECT_OK(function->close(nullptr, table_state));
    }
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_null_start_node_does_not_bypass_prefix_count_rejection) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1}, {2}, {}};

    const std::vector<std::optional<std::vector<std::optional<int64_t>>>> visited_node_variants = {
            std::vector<std::optional<int64_t>>{0L}, std::nullopt};
    for (const auto& visited_nodes : visited_node_variants) {
        const auto [table_state, function] = PrepareRows({{.adj_matrix = adj_matrix,
                                                           .start_node = std::nullopt,
                                                           .end_nodes = {2L},
                                                           .visited_nodes = visited_nodes}},
                                                         "N:PC2", /*pass_visited_nodes=*/true);
        Run(table_state, function.get());

        EXPECT_TRUE(table_state->status().is_invalid_argument())
                << "A prefix count was accepted alongside the visited_nodes argument on a NULL start_node row";
        EXPECT_OK(function->close(nullptr, table_state));
    }
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_constraints_are_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const std::vector<std::vector<std::optional<int64_t>>> block_second_edge = {{0, 1}, {0}, {0}, {}};
    const auto [table_state, function] = PrepareRows(
            {{.adj_matrix = adj_matrix, .start_node = 0L, .end_nodes = {3L}, .constraints = std::nullopt},
             {.adj_matrix = adj_matrix, .start_node = 0L, .end_nodes = {3L}, .constraints = block_second_edge}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}, {0L, 1L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 3}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_constraints_mismatch_rejected) {
    const auto [table_state, function] =
            PrepareRows({{.adj_matrix = {{1, 2}, {3}, {3}, {}},
                          .start_node = 0L,
                          .end_nodes = {3L},
                          .constraints = std::vector<std::vector<std::optional<int64_t>>>{{0}, {0}, {0}, {}}}});
    Run(table_state, function.get());

    EXPECT_TRUE(table_state->status().is_invalid_argument());
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, row_per_start_node_invalid_start_node_rejected) {
    const auto [table_state, function] = PrepareRows({{.adj_matrix = {{1}, {}}, .start_node = 0L, .end_nodes = {1L}},
                                                      {.adj_matrix = {{1}, {}}, .start_node = 2L, .end_nodes = {1L}}});
    Run(table_state, function.get());

    EXPECT_TRUE(table_state->status().is_invalid_argument());
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 1 --+
//        |        v
//        0        3
//        |        ^
//        +--> 2 --+
TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_groups_output_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] =
            PrepareArrayRows({{.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{3L}}},
                              {.adj_matrix = adj_matrix, .start_nodes = {{1L}}, .end_nodes = {{3L}}}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}, {1L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 3}));
    EXPECT_EQ(table_state->processed_rows(), 2);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_batching_start_nodes_does_not_change_the_paths) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const std::vector<std::vector<int64_t>> expected_paths = {{0L, 1L, 3L}, {0L, 2L, 3L}, {1L, 3L}, {2L, 3L}};

    const std::vector<std::vector<std::vector<std::optional<int64_t>>>> batchings = {
            {{0L, 1L, 2L}}, {{0L, 1L}, {2L}}, {{0L}, {1L}, {2L}}};
    for (const auto& batching : batchings) {
        std::vector<ArrayLayoutRow> rows;
        rows.reserve(batching.size());
        for (const auto& start_nodes : batching) {
            rows.push_back({.adj_matrix = adj_matrix, .start_nodes = start_nodes, .end_nodes = {{3L}}});
        }
        const auto [table_state, function] = PrepareArrayRows(rows);
        const auto [result_columns, offsets] = Run(table_state, function.get());

        EXPECT_OK(table_state->status());
        EXPECT_EQ(CollectPaths<TYPE_BIGINT>(result_columns[0]), expected_paths)
                << "Batching the start nodes into " << batching.size() << " rows changed the paths";
        EXPECT_EQ(table_state->processed_rows(), batching.size());
        EXPECT_OK(function->close(nullptr, table_state));
    }
}

TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_arguments_are_read_per_input_row) {
    const auto [table_state, function] = PrepareArrayRows(
            {{.adj_matrix = {{1}, {}}, .start_nodes = {{0L}}, .end_nodes = {{1L}}, .config_str = "N:EQ:2"},
             {.adj_matrix = {{1}, {2}, {}}, .start_nodes = {{0L}}, .end_nodes = {{2L}}, .config_str = "N:EQ:3"},
             {.adj_matrix = {{1}, {}},
              .start_nodes = {{0L}},
              .end_nodes = {{1L}},
              .constraints = {{{1}, {}}},
              .config_str = "N:EQ:2"}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L}, {0L, 1L, 2L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1, 2, 2}));
    EXPECT_EQ(table_state->processed_rows(), 3);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_empty_start_nodes_emit_nothing) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] =
            PrepareArrayRows({{.adj_matrix = adj_matrix, .start_nodes = {{1L}}, .end_nodes = {{3L}}},
                              {.adj_matrix = adj_matrix, .start_nodes = std::nullopt, .end_nodes = {{3L}}},
                              {.adj_matrix = adj_matrix, .start_nodes = {{2L}}, .end_nodes = {{3L}}}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{1L, 3L}, {2L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1, 1, 2}));
    EXPECT_EQ(table_state->processed_rows(), 3);
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_multiple_rows_yield_across_chunks) {
    rt_state_->set_chunk_size(2);
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] =
            PrepareArrayRows({{.adj_matrix = adj_matrix, .start_nodes = {{0L, 1L}}, .end_nodes = {{3L}}},
                              {.adj_matrix = adj_matrix, .start_nodes = {{2L}}, .end_nodes = {{3L}}}});

    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}});
        EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2}));
        EXPECT_EQ(table_state->processed_rows(), 0);
    }

    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{1L, 3L}, {2L, 3L}});
        EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1, 2}));
        EXPECT_EQ(table_state->processed_rows(), 1);
    }

    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {});
        EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 0}));
        EXPECT_EQ(table_state->processed_rows(), 2);
    }

    EXPECT_OK(table_state->status());
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_visited_nodes_rejected_with_prefix_count) {
    const std::vector<std::optional<std::vector<std::vector<std::optional<int64_t>>>>> visited_node_variants = {
            std::vector<std::vector<std::optional<int64_t>>>{{0L}}, std::nullopt};
    for (const auto& visited_nodes : visited_node_variants) {
        const auto [table_state, function] = PrepareArrayRows({{.adj_matrix = {{1}, {2}, {}},
                                                                .start_nodes = {{0L}},
                                                                .end_nodes = {{2L}},
                                                                .config_str = "N:PC2",
                                                                .visited_nodes = visited_nodes}},
                                                              /*pass_visited_nodes=*/true);
        Run(table_state, function.get());

        EXPECT_TRUE(table_state->status().is_invalid_argument())
                << "A prefix count was accepted alongside the visited_nodes argument";
        EXPECT_OK(function->close(nullptr, table_state));
    }
}

TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_visited_nodes_are_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    using VisitedNodes = std::vector<std::vector<std::optional<int64_t>>>;
    const auto [table_state, function] = PrepareArrayRows({{.adj_matrix = adj_matrix,
                                                            .start_nodes = {{0L}},
                                                            .end_nodes = {{3L}},
                                                            .visited_nodes = VisitedNodes{{1L}}},
                                                           {.adj_matrix = adj_matrix,
                                                            .start_nodes = {{0L}},
                                                            .end_nodes = {{3L}},
                                                            .visited_nodes = VisitedNodes{{}}}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 2L, 3L}, {0L, 1L, 3L}, {0L, 2L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1, 3}));
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 3
//        |
//   +--> 1 --> 4        5 (the only end node, and unreachable)
//   |
//   0 --> 2
TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_prefix_count_applies_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3, 4}, {}, {}, {}, {}};
    const auto [table_state, function] = PrepareArrayRows(
            {{.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{5L}}, .config_str = "N:PC2"},
             {.adj_matrix = adj_matrix, .start_nodes = {{1L}}, .end_nodes = {{5L}}, .config_str = "N:PC2"}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L}, {0L, 2L}, {1L, 3L}, {1L, 4L}});
    EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({1L, 2L, 3L, 4L}));
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 4}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, array_layout_prefix_count_chunked_output_spans_rows) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3, 4}, {}, {}, {}, {}};
    rt_state_->set_chunk_size(1);
    const auto [table_state, function] = PrepareArrayRows(
            {{.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{5L}}, .config_str = "N:PC2"},
             {.adj_matrix = adj_matrix, .start_nodes = {{1L}}, .end_nodes = {{5L}}, .config_str = "N:PC2"}});

    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L}});
        EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({1L}));
        EXPECT_EQ(table_state->processed_rows(), 0);
        EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1}));
    }
    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 2L}});
        EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({2L}));
        EXPECT_EQ(table_state->processed_rows(), 1);
        EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1}));
    }
    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{1L, 3L}});
        EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({3L}));
        EXPECT_EQ(table_state->processed_rows(), 1);
        EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1}));
    }
    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{1L, 4L}});
        EXPECT_EQ(CollectResumeNodes(result_columns[2]), std::vector<std::optional<int64_t>>({4L}));
        EXPECT_EQ(table_state->processed_rows(), 2);
        EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1}));
    }

    EXPECT_OK(table_state->status());
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
//        +--> 1 --+
//        |        v
//        0        3
//        |        ^
//        +--> 2 --+
TEST_F(CelonisObjectLinkExtractPathsTest, end_nodes_are_resolved_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] =
            PrepareArrayRows({{.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{3L}}},
                              {.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{1L}}},
                              {.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{3L}}}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}, {0L, 1L}, {0L, 1L, 3L}, {0L, 2L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 3, 5}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, end_nodes_are_revalidated_against_each_rows_graph) {
    const auto [table_state, function] =
            PrepareArrayRows({{.adj_matrix = {{1}, {2}, {3}, {4}, {}}, .start_nodes = {{0L}}, .end_nodes = {{4L}}},
                              {.adj_matrix = {{1}, {}}, .start_nodes = {{0L}}, .end_nodes = {{4L}}}});
    Run(table_state, function.get());

    EXPECT_TRUE(table_state->status().is_invalid_argument()) << "An end node outside the row's own graph was accepted";
    EXPECT_OK(function->close(nullptr, table_state));
}

// Graph Representation:
// 0 ---> 1 ---> 2
TEST_F(CelonisObjectLinkExtractPathsTest, config_is_resolved_per_input_row) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1}, {2}, {}};
    const auto [table_state, function] = PrepareArrayRows(
            {{.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{2L}}, .config_str = "N:EQ:3"},
             {.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{2L}}, .config_str = "N:EQ:2"},
             {.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{2L}}, .config_str = "N:EQ:3"}});
    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 2L}, {0L, 1L, 2L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 1, 1, 2}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, stale_traversal_state_is_cleared_when_new_params_arrive) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    rt_state_->set_chunk_size(1);
    const auto [table_state, function] = Prepare<TYPE_BIGINT>(adj_matrix, {{0L}}, {{3L}});

    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}});
        EXPECT_EQ(table_state->processed_rows(), 0);
    }

    rt_state_->set_chunk_size(4096);
    table_state->set_params(BuildArrayLayoutColumns<TYPE_BIGINT>(adj_matrix, {{0L}}, {{3L}}));

    {
        const auto [result_columns, offsets] = Run(table_state, function.get());
        EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}});
        EXPECT_EQ(table_state->processed_rows(), 1);
    }

    EXPECT_OK(table_state->status());
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, config_argument_as_const_column_is_resolved_once) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] = PrepareArrayRows(
            {{.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{3L}}, .config_str = "N:EQ:3"},
             {.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{1L}}, .config_str = "N:EQ:3"}});

    auto config_elements = BinaryColumn::create();
    config_elements->append("N:EQ:3");
    auto config_nulls = NullColumn::create();
    config_nulls->append(0);
    auto single_config = NullableColumn::create(std::move(config_elements), std::move(config_nulls));
    table_state->get_columns()[4] = ConstColumn::create(std::move(single_config), 2);

    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 2}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, start_node_argument_as_const_column_in_row_per_start_node_layout) {
    const auto [table_state, function] =
            PrepareRows({{.adj_matrix = {{1, 2}, {3}, {3}, {}}, .start_node = 0L, .end_nodes = {{3L}}},
                         {.adj_matrix = {{1}, {}}, .start_node = 0L, .end_nodes = {{1L}}}});

    auto single_start = NullableColumn::create(Int64Column::create(), NullColumn::create());
    single_start->append_datum(int64_t{0});
    table_state->get_columns()[1] = ConstColumn::create(std::move(single_start), 2);

    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}, {0L, 1L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 3}));
    EXPECT_OK(function->close(nullptr, table_state));
}

TEST_F(CelonisObjectLinkExtractPathsTest, constraint_argument_as_const_null_column_is_treated_as_no_constraints) {
    const std::vector<std::vector<std::optional<int64_t>>> adj_matrix = {{1, 2}, {3}, {3}, {}};
    const auto [table_state, function] =
            PrepareArrayRows({{.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{3L}}},
                              {.adj_matrix = adj_matrix, .start_nodes = {{0L}}, .end_nodes = {{1L}}}});

    // A whole-column constant NULL, in the same nested shape BuildArrayLayoutColumns() uses for
    // "no constraints supplied", but delivered as a ConstColumn instead of one row per input row.
    auto inner_elements = NullableColumn::create(Int64Column::create(), NullColumn::create());
    auto inner_offsets = UInt32Column::create();
    auto inner_array = ArrayColumn::create(std::move(inner_elements), std::move(inner_offsets));
    auto outer_elements_nulls = NullColumn::create(inner_array->size(), 0);
    auto outer_elements = NullableColumn::create(std::move(inner_array), std::move(outer_elements_nulls));
    auto outer_offsets = UInt32Column::create();
    outer_offsets->append(0);
    outer_offsets->append(0);
    auto outer_nulls = NullColumn::create();
    outer_nulls->append(1);
    auto single_null_constraint = NullableColumn::create(
            ArrayColumn::create(std::move(outer_elements), std::move(outer_offsets)), std::move(outer_nulls));
    table_state->get_columns()[3] = ConstColumn::create(std::move(single_null_constraint), 2);

    const auto [result_columns, offsets] = Run(table_state, function.get());

    EXPECT_OK(table_state->status());
    EvaluatePaths<TYPE_BIGINT>(result_columns[0], {{0L, 1L, 3L}, {0L, 2L, 3L}, {0L, 1L}});
    EXPECT_EQ(CollectRowOffsets(offsets), std::vector<uint32_t>({0, 2, 3}));
    EXPECT_OK(function->close(nullptr, table_state));
}

} // namespace starrocks