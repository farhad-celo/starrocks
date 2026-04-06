#include "exprs/celonis/utils/json_to_model.h"

#include <cpml/model/bpmn/bpmn_graph_builder.h>
#include <google/protobuf/util/json_util.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <boost/multi_index_container.hpp>
#include <nlohmann/json.hpp>
#include <random>
#include <span>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/celonis/id.h"
#include "exprs/celonis/utils/proto_utils.h"
#include "json_model_creator.h"
#include "modules/query/operators.pb.h"
#include "runtime/mem_pool.h"
#include "util/slice.h"

namespace starrocks {

namespace {
using ProtoNodeType = ::celonis::accelerator::BpmnModelDescription_BpmnNode_BpmnNodeType;
using ProtoNode = ::celonis::accelerator::BpmnModelDescription_BpmnNode;
using ProtoEdge = ::celonis::accelerator::BpmnModelDescription_BpmnEdge;
using NodeId = int64;
using NodeType = std::tuple<NodeId, ProtoNodeType, std::string>;
using EdgeType = pair<NodeId, NodeId>;

using json = nlohmann::json;

celonis::bpmn_model_description create_model_description(const std::span<const NodeType> nodes,
                                                         const std::span<const EdgeType> edges) {
    ::celonis::accelerator::BpmnModelDescription model{};

    for (const auto& node : nodes) {
        ProtoNode* new_node{model.add_nodes()};
        new_node->set_node_id(std::get<0>(node));
        new_node->set_node_type(std::get<1>(node));
        switch (std::get<1>(node)) {
        case ::celonis::accelerator::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK:
            new_node->set_task_name(std::get<2>(node));
            break;
        default:
            break;
        }
    }

    for (const auto& edge : edges) {
        ProtoEdge* new_edge{model.add_edges()};
        new_edge->set_from(std::get<0>(edge));
        new_edge->set_to(std::get<1>(edge));
    }

    return celonis::bpmn_model_description::from_proto(model);
}

typedef std::vector<std::vector<std::string>> VariantRows;

ArrayColumn::Ptr build_variant_column(const std::vector<std::vector<std::string>>& rows) {
    ColumnBuilder<TYPE_VARCHAR> builder(config::vector_chunk_size);
    auto offsets = UInt32Column::create();
    int offset = 0;

    offsets->append(offset);
    for (int i = 0; i < rows.size(); i++) {
        for (int j = 0; j < rows[i].size(); j++) {
            if (rows[i][j] == "null") {
                builder.append_null();
            } else {
                builder.append(Slice(rows[i][j]));
            }
        }
        offset += rows[i].size();
        offsets->append(offset);
    }

    auto data_col = builder.build_nullable_column();
    return ArrayColumn::create(data_col, offsets);
}

celonis::details::dictionary create_dictionary(const Column* activity_column) {
    celonis::details::dictionary dict{};

    const size_t num_rows{activity_column->size()};
    for (size_t row{0}; row < num_rows; row++) {
        const Datum& trace{activity_column->get(row)};
        if (trace.is_null()) {
            continue;
        }

        for (const auto& trace_names{trace.get_array()}; const auto& activity : trace_names) {
            if (activity.is_null()) {
                continue;
            }
            dict.insert(activity.get_slice());
        }
    }
    return dict;
}

const std::string PARALLEL_MODEL =
        R"json({
            "nodes": [
                {
                    "node_id": "0",
                    "node_type": 4
                },
                {
                    "node_id": "1",
                    "node_type": 1,
                    "task_name": "A"
                },
                {
                    "node_id": "2",
                    "node_type": 3
                },
                {
                    "node_id": "3",
                    "node_type": 1,
                    "task_name": "B"
                },
                {
                    "node_id": "4",
                    "node_type": 1,
                    "task_name": "C"
                },
                {
                    "node_id": "5",
                    "node_type": 3
                },
                {
                    "node_id": "6",
                    "node_type": 5
                }
            ],
            "edges": [
                {
                    "from": "0",
                    "to": "1"
                },
                {
                    "from": "1",
                    "to": "2"
                },
                {
                    "from": "2",
                    "to": "3"
                },
                {
                    "from": "2",
                    "to": "4"
                },
                {
                    "from": "3",
                    "to": "5"
                },
                {
                    "from": "4",
                    "to": "5"
                },
                {
                    "from": "5",
                    "to": "6"
                }
            ],
            "cache_key": "CACHE_KEY"
        })json";

const std::string INCORRECT_PARALLEL_MODEL =
        R"json({
            "nodes": [
                {
                    "node_id": "1",
                    "node_type": 1,
                    "task_name": "A"
                },
                {
                    "node_id": "2",
                    "node_type": 3
                },
                {
                    "node_id": "3",
                    "node_type": 1,
                    "task_name": "B"
                },
                {
                    "node_id": "4",
                    "node_type": 1,
                    "task_name": "C"
                },
                {
                    "node_id": "5",
                    "node_type": 3
                },
                {
                    "node_id": "6",
                    "node_type": 5
                }
            ],
            "edges": [
                {
                    "from": "0",
                    "to": "1"
                },
                {
                    "from": "1",
                    "to": "2"
                },
                {
                    "from": "2",
                    "to": "3"
                },
                {
                    "from": "2",
                    "to": "4"
                },
                {
                    "from": "3",
                    "to": "5"
                },
                {
                    "from": "4",
                    "to": "5"
                },
                {
                    "from": "5",
                    "to": "6"
                }
            ],
            "cache_key": "CACHE_KEY"
        })json";
} // namespace

TEST(JSONToModelCreatorTest, DictionaryCreatorTestBasic) {
    VariantRows variant_rows = {{"A", "C", "D"}, {"A", "D", "C"}, {"A", "A", "C", "D"}, {"B", "C", "D"}};
    auto variants = build_variant_column(variant_rows);
    auto dict = create_dictionary(variants.get());
    const std::vector<std::string> dict_expected{"A", "B", "C", "D"};
    for (int i{0}; const auto& activity : dict) {
        ASSERT_EQ(activity, dict_expected[i]);
        ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, dict_expected[i]), i + 1);
        ++i;
    }
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "E"), -1);
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "null"), 0);
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "NULL"), 0);
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "Null"), 0);
}

TEST(JSONToModelCreatorTest, DictionaryCreatorTestWithNullElementInVariants) {
    VariantRows variant_rows = {{"A", "C", "D"}, {"null", "D", "C"}, {"A", "A", "C", "D"}, {"B", "C", "D"}};
    auto variants = build_variant_column(variant_rows);
    auto dict = create_dictionary(variants.get());
    const std::vector<std::string> dict_expected{"A", "B", "C", "D"};
    for (int i{0}; const auto& activity : dict) {
        ASSERT_EQ(activity, dict_expected[i]);
        ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, dict_expected[i]), i + 1);
        ++i;
    }
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "E"), -1);
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "null"), 0);
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "NULL"), 0);
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "Null"), 0);
}

TEST(JSONToModelCreatorTest, ConvertFromProtoBpmnDescriptionToBpmn) {
    std::vector<NodeType> nodes{{0, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_START, ""},
                                {1, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_END, ""},
                                {2, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "Create Order"},
                                {4, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "Process Order"},
                                {3, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""},
                                {5, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "Cancel Order"},
                                {6, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""}};
    std::vector<EdgeType> edges{{0, 2}, {2, 3}, {3, 4}, {3, 5}, {4, 6}, {5, 6}, {6, 1}};
    const celonis::bpmn_model_description bpmn_model_description{create_model_description(nodes, edges)};

    VariantRows variant_rows = {{"Create Order", "Cancel Order"}, {"null", "Create Order", "Process Order"}};
    auto variants = build_variant_column(variant_rows);
    const celonis::details::dictionary dict = create_dictionary(variants.get());

    cpml::model::bpmn::bpmn_graph_builder builder;
    const auto start_id{builder.add_vertex(cpml::model::bpmn::start{})};
    const auto end_id{builder.add_vertex(cpml::model::bpmn::end{})};
    const auto create_order_id{builder.add_vertex(cpml::model::bpmn::task(2, 2))};
    const auto exclusive_1_id{builder.add_vertex(cpml::model::bpmn::exclusive_choice{})};
    const auto process_order_id{builder.add_vertex(cpml::model::bpmn::task(3, 1))};
    const auto cancel_order_id{builder.add_vertex(cpml::model::bpmn::task(1, 1))};
    const auto exclusive_2_id{builder.add_vertex(cpml::model::bpmn::exclusive_choice{})};

    builder.edge(cpml::model::bpmn::edge{start_id, create_order_id});
    builder.edge(cpml::model::bpmn::edge{create_order_id, exclusive_1_id});
    builder.edge(cpml::model::bpmn::edge{exclusive_1_id, process_order_id});
    builder.edge(cpml::model::bpmn::edge{exclusive_1_id, cancel_order_id});
    builder.edge(cpml::model::bpmn::edge{process_order_id, exclusive_2_id});
    builder.edge(cpml::model::bpmn::edge{cancel_order_id, exclusive_2_id});
    builder.edge(cpml::model::bpmn::edge{exclusive_2_id, end_id});

    cpml::model::bpmn_graph graph_expected{builder.build()};

    const auto [result_graph,
                _]{celonis::details::convert_from_proto_and_create_string_map(bpmn_model_description, dict)};

    ASSERT_EQ(graph_expected.get_vertices(), result_graph.get_vertices());
    ASSERT_EQ(graph_expected.get_edges(), result_graph.get_edges());
}

TEST(JSONToModelCreatorTest, ConvertFromProtoBpmnDescriptionToBpmnWithLargerVariantRows) {
    std::vector<NodeType> nodes{{0, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_START, ""},
                                {1, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_END, ""},
                                {2, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "Create Order"},
                                {4, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "Process Order"},
                                {3, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""},
                                {5, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "Cancel Order"},
                                {6, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""}};
    std::vector<EdgeType> edges{{0, 2}, {2, 3}, {3, 4}, {3, 5}, {4, 6}, {5, 6}, {6, 1}};
    const celonis::bpmn_model_description bpmn_model_description{create_model_description(nodes, edges)};

    VariantRows variant_rows = {{"Create Order", "Cancel Order"},
                                {"null", "Create Order", "Process Order", "Cancel Order"},
                                {"Process Order", "Cancel Order"}};
    auto variants = build_variant_column(variant_rows);
    const celonis::details::dictionary dict = create_dictionary(variants.get());

    cpml::model::bpmn::bpmn_graph_builder builder;
    const auto start_id{builder.add_vertex(cpml::model::bpmn::start{})};
    const auto end_id{builder.add_vertex(cpml::model::bpmn::end{})};
    const auto create_order_id{builder.add_vertex(cpml::model::bpmn::task(2, 2))};
    const auto exclusive_1_id{builder.add_vertex(cpml::model::bpmn::exclusive_choice{})};
    const auto process_order_id{builder.add_vertex(cpml::model::bpmn::task(3, 1))};
    const auto cancel_order_id{builder.add_vertex(cpml::model::bpmn::task(1, 1))};
    const auto exclusive_2_id{builder.add_vertex(cpml::model::bpmn::exclusive_choice{})};

    builder.edge(cpml::model::bpmn::edge{start_id, create_order_id});
    builder.edge(cpml::model::bpmn::edge{create_order_id, exclusive_1_id});
    builder.edge(cpml::model::bpmn::edge{exclusive_1_id, process_order_id});
    builder.edge(cpml::model::bpmn::edge{exclusive_1_id, cancel_order_id});
    builder.edge(cpml::model::bpmn::edge{process_order_id, exclusive_2_id});
    builder.edge(cpml::model::bpmn::edge{cancel_order_id, exclusive_2_id});
    builder.edge(cpml::model::bpmn::edge{exclusive_2_id, end_id});

    cpml::model::bpmn_graph graph_expected{builder.build()};

    const auto [result_graph,
                _]{celonis::details::convert_from_proto_and_create_string_map(bpmn_model_description, dict)};

    ASSERT_EQ(graph_expected.get_vertices(), result_graph.get_vertices());
    ASSERT_EQ(graph_expected.get_edges(), result_graph.get_edges());
}

TEST(JSONToModelCreatorTest, TestingBpmnStringFromConvertFromProtoAndCreateStringMapFunction) {
    nlohmann::json json_model{nlohmann::json::parse(PARALLEL_MODEL)};
    json_model = json_model[0];
    celonis::details::dictionary dict{};
    dict.insert("A");
    dict.insert("B");
    dict.insert("C");
    const auto bpmn_proto_st{celonis::details::transform_to_proto_bpmn(json_model)};

    ASSERT_TRUE(bpmn_proto_st.ok());
    const auto [_,
                bpmn_string]{celonis::details::convert_from_proto_and_create_string_map(bpmn_proto_st.value(), dict)};

    celonis::details::bpmn_to_string_t bpmn_string_expected{};
    bpmn_string_expected[0] = "BPMN_START";
    bpmn_string_expected[1] = "A";
    bpmn_string_expected[2] = "BPMN_PARALLEL";
    bpmn_string_expected[3] = "B";
    bpmn_string_expected[4] = "C";
    bpmn_string_expected[5] = "BPMN_PARALLEL";
    bpmn_string_expected[6] = "BPMN_END";

    ASSERT_EQ(bpmn_string, bpmn_string_expected);
}

TEST(JSONToModelCreatorTest, TestingPARALLELMODELsCreationWithConvertFromProtoFunction) {
    nlohmann::json json_model{nlohmann::json::parse(PARALLEL_MODEL)};
    json_model = json_model[0];
    celonis::details::dictionary dict{};
    dict.insert("A");
    dict.insert("B");
    dict.insert("C");

    std::vector<NodeType> nodes{{0, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_START, ""},
                                {1, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "A"},
                                {2, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                {3, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "B"},
                                {4, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "C"},
                                {5, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                {6, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_END, ""}};
    std::vector<EdgeType> edges{{0, 1}, {1, 2}, {2, 3}, {2, 4}, {3, 5}, {4, 5}, {5, 6}};

    const celonis::bpmn_model_description bpmn_proto_expected{create_model_description(nodes, edges)};

    const auto bpmn_proto_st{celonis::details::transform_to_proto_bpmn(json_model)};
    ASSERT_TRUE(bpmn_proto_st.ok());
    const auto [bpmn,
                bpmn_string_v]{celonis::details::convert_from_proto_and_create_string_map(bpmn_proto_st.value(), dict)};
    const auto [bpmn_expected,
                bpmn_string]{celonis::details::convert_from_proto_and_create_string_map(bpmn_proto_expected, dict)};

    ASSERT_EQ(bpmn_string_v, bpmn_string);
    ASSERT_EQ(bpmn.get_vertices(), bpmn_expected.get_vertices());
    ASSERT_EQ(bpmn.get_edges(), bpmn_expected.get_edges());
}

TEST(JSONToModelCreatorTest, TestingPARALLELMODELsCreationWithTransformToBpmnProto) {
    nlohmann::json json_model{nlohmann::json::parse(PARALLEL_MODEL)};
    json_model = json_model[0];

    std::vector<NodeType> nodes{{0, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_START, ""},
                                {1, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "A"},
                                {2, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                {3, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "B"},
                                {4, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "C"},
                                {5, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                {6, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_END, ""}};
    std::vector<EdgeType> edges{{0, 1}, {1, 2}, {2, 3}, {2, 4}, {3, 5}, {4, 5}, {5, 6}};

    const celonis::bpmn_model_description bpmn_proto_expected{create_model_description(nodes, edges)};
    cpml::model::bpmn_graph bpmn_expected{celonis::details::transform_to_bpmn_graph(bpmn_proto_expected)};

    const auto bpmn_proto_st{celonis::details::transform_to_proto_bpmn(json_model)};
    ASSERT_TRUE(bpmn_proto_st.ok());
    cpml::model::bpmn_graph bpmn{celonis::details::transform_to_bpmn_graph(bpmn_proto_st.value())};

    ASSERT_EQ(bpmn.get_vertices(), bpmn_expected.get_vertices());
    ASSERT_EQ(bpmn.get_edges(), bpmn_expected.get_edges());
}

TEST(JSONToModelCreatorTest, TestingPARALLELMODELsCreationWithTransformToBpmnGraphJson) {
    nlohmann::json json_model{nlohmann::json::parse(PARALLEL_MODEL)};
    json_model = json_model[0];
    std::vector<NodeType> nodes{{0, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_START, ""},
                                {1, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "A"},
                                {2, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                {3, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "B"},
                                {4, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "C"},
                                {5, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                {6, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_END, ""}};
    std::vector<EdgeType> edges{{0, 1}, {1, 2}, {2, 3}, {2, 4}, {3, 5}, {4, 5}, {5, 6}};

    const celonis::bpmn_model_description bpmn_proto_expected{create_model_description(nodes, edges)};
    const auto bpmn_expected{celonis::details::transform_to_bpmn_graph(bpmn_proto_expected)};

    const auto bpmn_graph_st{celonis::transform_to_bpmn_graph(json_model)};
    ASSERT_TRUE(bpmn_graph_st.ok());
    const cpml::model::bpmn_graph& bpmn{bpmn_graph_st.value()};

    ASSERT_EQ(bpmn.get_vertices(), bpmn_expected.get_vertices());
    ASSERT_EQ(bpmn.get_edges(), bpmn_expected.get_edges());
}

////// EDGE CASES

TEST(JSONToModelCreatorTest, DictionaryCreatorTestEmptyVariants) {
    VariantRows variant_rows = {{}}; //{{"A", "C", "D"}, {"A", "D", "C"}, {"A", "A", "C", "D"}, {"B", "C", "D"}};
    auto variants = build_variant_column(variant_rows);
    auto dict = create_dictionary(variants.get());
    const std::vector<std::string> dict_expected{"A", "B", "C", "D"};
    for (int i{0}; const auto& activity : dict_expected) {
        ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, activity), -1);
        ++i;
    }
    ASSERT_EQ(celonis::details::dictionary_get_row_id_for(dict, "null"), 0);
}

TEST(JSONToModelCreatorTest, ConvertFromProtoBpmnDescriptionToBpmnEmptyNodes) {
    std::vector<NodeType> nodes{};
    std::vector<EdgeType> edges{{0, 2}, {2, 3}, {3, 4}, {3, 5}, {4, 6}, {5, 6}, {6, 1}};

    VariantRows variant_rows = {{"Create Order", "Cancel Order"}, {"null", "Create Order", "Process Order"}};
    auto variants = build_variant_column(variant_rows);
    const celonis::details::dictionary dict = create_dictionary(variants.get());

    EXPECT_ANY_THROW(
            celonis::details::convert_from_proto_and_create_string_map(create_model_description(nodes, edges), dict));
}

TEST(JSONToModelCreatorTest, TestingPARALLELMODELsCreationWithIncorrectJSON) {
    nlohmann::json json_model{nlohmann::json::parse(INCORRECT_PARALLEL_MODEL)};
    json_model = json_model[0];

    EXPECT_NO_THROW(celonis::details::transform_to_proto_bpmn(json_model));
    EXPECT_ANY_THROW(
            celonis::details::transform_to_bpmn_graph(celonis::details::transform_to_proto_bpmn(json_model).value()));
}

/// https://celonis.atlassian.net/browse/PMT-3003
[[nodiscard]] celonis::bpmn_model_description make_unsound_model_from_PMT_3003() {
    using enum ::celonis::accelerator::BpmnModelDescription_BpmnNode_BpmnNodeType;
    const std::vector<NodeType> nodes{{0, BpmnModelDescription_BpmnNode_BpmnNodeType_START, ""},
                                      {1, BpmnModelDescription_BpmnNode_BpmnNodeType_END, ""},
                                      {2, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "A"},
                                      {3, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "B"},
                                      {4, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "C"},
                                      {5, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "D"},
                                      {6, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "E"},
                                      {7, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "F"},
                                      {8, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "G"},
                                      {9, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "H"},
                                      {10, BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "I"},
                                      {11, BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""},
                                      {12, BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""},
                                      {13, BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                      {14, BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL, ""},
                                      {15, BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""},
                                      {16, BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""},
                                      {17, BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""},
                                      {18, BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE, ""}};
    const std::vector<EdgeType> edges{
            {0, 4},   {2, 16},  {3, 9},   {4, 14},  {5, 11},  {6, 13}, {7, 1},  {9, 5},
            {10, 18}, {11, 12}, {11, 17}, {12, 15}, {13, 3},  {14, 6}, {14, 8}, {15, 2},
            {15, 16}, {16, 7},  {17, 10}, {17, 18}, {18, 12}, {8, 13} /* [8, 12] would be correct here */};
    return celonis::bpmn_model_description{create_model_description(nodes, edges)};
}

TEST(JSONToModelCreatorTest, PMT3003_UnsoundModelTransformation) {
    // GIVEN
    const auto unsound_bpmn_proto{make_unsound_model_from_PMT_3003()};

    // WHEN - THEN (N.B.: In the future we might throw in the CPML; in that case, we should revisit the SR code paths)
    ASSERT_NO_THROW(celonis::details::transform_to_bpmn_graph(unsound_bpmn_proto));
}

} // namespace starrocks