#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace celonis::accelerator {
class BpmnModelDescription;
} // namespace celonis::accelerator

namespace starrocks::celonis {

/** Proxy for the proto BpmnModelDescription message */
class bpmn_model_description {
public:
    using node_id_t = std::int64_t;

    class bpmn_node {
    public:
        enum bpmn_node_type { TASK = 1, EXCLUSIVE_CHOICE = 2, PARALLEL = 3, START = 4, END = 5 };

        bpmn_node(node_id_t node_id, bpmn_node_type node_type,
                  std::optional<std::string> optional_task_name = std::nullopt);

        [[nodiscard]] node_id_t node_id() const noexcept { return node_id_; }
        [[nodiscard]] bpmn_node_type node_type() const noexcept { return node_type_; }
        [[nosdicard]] bool has_task_name() const noexcept { return optional_tak_name_.has_value(); }
        [[nodiscard]] const std::string& task_name() const { return optional_tak_name_.value(); }

    private:
        node_id_t node_id_{};
        bpmn_node_type node_type_{};
        std::optional<std::string> optional_tak_name_{};
    };

    using bpmn_nodes_t = std::vector<bpmn_node>;

    class bpmn_edge {
    public:
        bpmn_edge(const node_id_t from, const node_id_t to) noexcept : from_{from}, to_{to} {}

        [[nodiscard]] node_id_t from() const noexcept { return from_; }
        [[nodiscard]] node_id_t to() const noexcept { return to_; }

    private:
        node_id_t from_{};
        node_id_t to_{};
    };

    using bpmn_edges_t = std::vector<bpmn_edge>;

    bpmn_model_description(bpmn_nodes_t bpmn_nodes, bpmn_edges_t bpmn_edges) noexcept
            : nodes_{std::move(bpmn_nodes)}, edges_{std::move(bpmn_edges)} {}

    /** Create the proxy from the proto message */
    [[nodiscard]] static bpmn_model_description from_proto(
            const ::celonis::accelerator::BpmnModelDescription& proto_bpmn_model_description);

    [[nodiscard]] const bpmn_nodes_t& nodes() const { return nodes_; }
    [[nodiscard]] const bpmn_edges_t& edges() const { return edges_; }

private:
    bpmn_nodes_t nodes_{};
    bpmn_edges_t edges_{};
};

} // namespace starrocks::celonis
