#include "exprs/celonis/utils/proto_utils.h"

#include <ctl/assert.h>

#include "json_to_model.h"
#include "modules/query/operators.pb.h"

namespace starrocks::celonis {

bpmn_model_description::bpmn_node::bpmn_node(const node_id_t node_id, const bpmn_node_type node_type,
                                             std::optional<std::string> optional_task_name)
        : node_id_{node_id}, node_type_{node_type}, optional_tak_name_{std::move(optional_task_name)} {
    ctl::runtime_assert(node_type_ != TASK || optional_tak_name_.has_value(),
                        "If the BPMN node is of type TASK it must also have a task name.");
    ctl::runtime_assert(node_type_ == TASK || !optional_tak_name_.has_value(),
                        "If the BPMN node is not of type TASK, it must not have a task name.");
}

namespace {

using proto_bpmn_description_t = ::celonis::accelerator::BpmnModelDescription;
using proto_bpmn_node_description_t = proto_bpmn_description_t::BpmnNode;
using proto_bpmn_edge_description_t = proto_bpmn_description_t::BpmnEdge;

[[nodiscard]] bpmn_model_description::bpmn_node::bpmn_node_type transform_from_proto(
        const proto_bpmn_description_t::BpmnNode::BpmnNodeType& proto_bpmn_node_type) {
    switch (proto_bpmn_node_type) {
        using enum proto_bpmn_description_t::BpmnNode::BpmnNodeType;
    case BpmnModelDescription_BpmnNode_BpmnNodeType_TASK:
        return bpmn_model_description::bpmn_node::TASK;
    case BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE:
        return bpmn_model_description::bpmn_node::EXCLUSIVE_CHOICE;
    case BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL:
        return bpmn_model_description::bpmn_node::PARALLEL;
    case BpmnModelDescription_BpmnNode_BpmnNodeType_START:
        return bpmn_model_description::bpmn_node::START;
    case BpmnModelDescription_BpmnNode_BpmnNodeType_END:
        return bpmn_model_description::bpmn_node::END;
    }
    ctl::assert_unreachable();
}

[[nodiscard]] bpmn_model_description::bpmn_node transform_from_proto(
        const proto_bpmn_description_t::BpmnNode& proto_bpmn_node_description) {
    return bpmn_model_description::bpmn_node{
            proto_bpmn_node_description.node_id(), transform_from_proto(proto_bpmn_node_description.node_type()),
            proto_bpmn_node_description.has_task_name() ? std::make_optional(proto_bpmn_node_description.task_name())
                                                        : std::nullopt};
}

[[nodiscard]] bpmn_model_description::bpmn_edge transform_from_proto(
        const proto_bpmn_description_t::BpmnEdge& proto_bpmn_edge_description) {
    return bpmn_model_description::bpmn_edge{proto_bpmn_edge_description.from(), proto_bpmn_edge_description.to()};
}

} // anonymous namespace

bpmn_model_description bpmn_model_description::from_proto(
        const ::celonis::accelerator::BpmnModelDescription& proto_bpmn_model_description) {
    bpmn_nodes_t nodes{};
    nodes.reserve(proto_bpmn_model_description.nodes_size());
    std::ranges::transform(proto_bpmn_model_description.nodes(), std::back_inserter(nodes),
                           [](const proto_bpmn_node_description_t& proto_bpmn_node_description) {
                               return transform_from_proto(proto_bpmn_node_description);
                           });

    bpmn_edges_t edges{};
    edges.reserve(proto_bpmn_model_description.edges_size());
    std::ranges::transform(proto_bpmn_model_description.edges(), std::back_inserter(edges),
                           [](const proto_bpmn_edge_description_t& proto_bpmn_edge_description) {
                               return transform_from_proto(proto_bpmn_edge_description);
                           });

    return bpmn_model_description{std::move(nodes), std::move(edges)};
}

} // namespace starrocks::celonis
