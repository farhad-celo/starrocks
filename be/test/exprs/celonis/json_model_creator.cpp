#include "json_model_creator.h"

#include <nlohmann/json.hpp>

namespace starrocks {

JsonModelCreator<celonis::accelerator::BpmnModelDescription>::JsonModelCreator() {
    json_model_["nodes"] = json::array();
    json_model_["edges"] = json::array();
    json_model_["cache_key"] = "CACHE_KEY";
}

void JsonModelCreator<celonis::accelerator::BpmnModelDescription>::add_node(
        const NodeId id, const ProtoNodeType type, const std::optional<std::string>& optional_task_name) {
    if (node_ids_.contains(id)) {
        throw std::invalid_argument("Node already exists");
    }

    if (optional_task_name.has_value()) {
        json_model_["nodes"].push_back({
                {"node_id", std::to_string(id)},
                {"node_type", type},
                {"task_name", optional_task_name.value()},
        });
    } else {
        json_model_["nodes"].push_back({
                {"node_id", std::to_string(id)},
                {"node_type", type},
        });
    }

    node_ids_.insert(id);
}

void JsonModelCreator<celonis::accelerator::BpmnModelDescription>::add_edge(const NodeId source, const NodeId target) {
    json_model_["edges"].push_back({{"from", std::to_string(source)}, {"to", std::to_string(target)}});
}

const json& JsonModelCreator<celonis::accelerator::BpmnModelDescription>::get_json() const {
    return json_model_;
}

std::string JsonModelCreator<celonis::accelerator::BpmnModelDescription>::build() const {
    constexpr int indent{4};
    return json_model_.dump(indent);
}

} // namespace starrocks