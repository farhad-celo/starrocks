#include "json_model_creator.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <random>

#include "column/column_builder.h"
#include "exprs/agg/nullable_aggregate.h"
#include "modules/query/operators.pb.h"
#include "runtime/mem_pool.h"

namespace starrocks {
namespace {
using ProtoNodeType = celonis::accelerator::BpmnModelDescription_BpmnNode_BpmnNodeType;
using NodeId = int64;
using json = nlohmann::json;

constexpr const char* PARALLEL_MODEL =
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
} // namespace

TEST(JSONModelCreatorTest, BpmnCreationParallelModelTest) {
    JsonModelCreator<celonis::accelerator::BpmnModelDescription> json_model_creator;
    json_model_creator.add_node(NodeId{0}, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_START);
    json_model_creator.add_node(NodeId{1}, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "A");
    json_model_creator.add_node(NodeId{2}, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL);
    json_model_creator.add_node(NodeId{3}, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "B");
    json_model_creator.add_node(NodeId{4}, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK, "C");
    json_model_creator.add_node(NodeId{5}, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL);
    json_model_creator.add_node(NodeId{6}, ProtoNodeType::BpmnModelDescription_BpmnNode_BpmnNodeType_END);
    json_model_creator.add_edge(NodeId{0}, NodeId{1});
    json_model_creator.add_edge(NodeId{1}, NodeId{2});
    json_model_creator.add_edge(NodeId{2}, NodeId{3});
    json_model_creator.add_edge(NodeId{2}, NodeId{4});
    json_model_creator.add_edge(NodeId{3}, NodeId{5});
    json_model_creator.add_edge(NodeId{4}, NodeId{5});
    json_model_creator.add_edge(NodeId{5}, NodeId{6});
    json parallel_model = json::parse(PARALLEL_MODEL);
    ASSERT_EQ(json_model_creator.get_json(), parallel_model);
}

} // namespace starrocks