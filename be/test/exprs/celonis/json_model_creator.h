#pragma once

#include <cpml/model/bpmn_graph.h>

#include <nlohmann/json.hpp>

#include "column/column_builder.h"
#include "modules/query/operators.pb.h"
#include "runtime/mem_pool.h"

namespace starrocks {

namespace {
using ProtoNodeType = ::celonis::accelerator::BpmnModelDescription_BpmnNode_BpmnNodeType;
using ProtoNode = ::celonis::accelerator::BpmnModelDescription_BpmnNode;
using ProtoEdge = ::celonis::accelerator::BpmnModelDescription_BpmnEdge;
using NodeId = int64;
using NodeType = std::tuple<NodeId, ProtoNodeType, std::string>;
using EdgeType = pair<NodeId, NodeId>;

using json = nlohmann::json;

} // namespace

template <typename T>
class JsonModelCreator;

template <>
class JsonModelCreator<::celonis::accelerator::BpmnModelDescription> {
public:
    JsonModelCreator();

    void add_node(NodeId id, ProtoNodeType type, const std::optional<std::string>& optional_task_name = std::nullopt);

    void add_edge(NodeId source, NodeId target);

    [[nodiscard]] const json& get_json() const;

    [[nodiscard]] std::string build() const;

private:
    json json_model_;
    std::set<NodeId> node_ids_;
};

template <>
class JsonModelCreator<cpml::model::bpmn_graph>
        : public JsonModelCreator<::celonis::accelerator::BpmnModelDescription> {};

} // namespace starrocks