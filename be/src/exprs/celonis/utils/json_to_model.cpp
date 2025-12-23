#include "json_to_model.h"

#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph.h>
#include <cpml/types.h>
#include <ctl/conversion.h>
#include <fmt/format.h>
#include <google/protobuf/util/json_util.h>

#include <boost/multi_index/ranked_index.hpp>
#include <boost/multi_index_container.hpp>
#include <string_view>

#include "column/column.h"
#include "column/datum.h"
#include "exprs/celonis/id.h"
#include "exprs/celonis/utils/proto_utils.h"
#include "modules/query/operators.pb.h"

namespace starrocks::celonis {

namespace details {

namespace {

[[nodiscard]] std::string convert_to_string(const bpmn_model_description::bpmn_node& proto_node) {
    switch (proto_node.node_type()) {
        using enum bpmn_model_description::bpmn_node::bpmn_node_type;
    case TASK:
        return proto_node.task_name();
    case EXCLUSIVE_CHOICE:
        return "BPMN_EXCLUSIVE_CHOICE";
    case PARALLEL:
        return "BPMN_PARALLEL";
    case START:
        return "BPMN_START";
    case END:
        return "BPMN_END";
    default:
        ctl::assert_unreachable();
    }
}
} // namespace

row_id dictionary_get_row_id_for(const dictionary& dict, const std::string_view task_name) {
    if (task_name == "null" || task_name == "NULL" || task_name == "Null") {
        return 0;
    }
    const auto it_element = dict.find(task_name);
    if (it_element == dict.end()) {
        return -1;
    }
    return dict.rank(it_element) + 1;
}

cpml::model::bpmn::vertex_type proto_node_to_vertex(const bpmn_model_description::bpmn_node& proto_bpmn_node,
                                                    const dictionary* dictionary) {
    switch (proto_bpmn_node.node_type()) {
        using enum bpmn_model_description::bpmn_node::bpmn_node_type;
    case TASK:
        if (dictionary == nullptr) {
            return cpml::model::bpmn::task{ctl::cast<cpml::activity_id_t>(Id::get(proto_bpmn_node.task_name()))};
        }
        return cpml::model::bpmn::task{
                ctl::cast<cpml::activity_id_t>(dictionary_get_row_id_for(*dictionary, proto_bpmn_node.task_name()))};
    case EXCLUSIVE_CHOICE:
        return cpml::model::bpmn::exclusive_choice{};
    case PARALLEL:
        return cpml::model::bpmn::parallel{};
    case START:
        return cpml::model::bpmn::start{};
    case END:
        return cpml::model::bpmn::end{};
    }
    ctl::assert_unreachable();
}

std::pair<cpml::model::bpmn_graph, bpmn_to_string_t> convert_from_proto_and_create_string_map(
        const bpmn_model_description& bpmn_proto, const dictionary& dict) {
    bpmn_to_string_t bpmn_to_string;

    std::vector<cpml::model::bpmn::vertex> vertices{};
    vertices.reserve(bpmn_proto.nodes().size());
    std::ranges::transform(bpmn_proto.nodes(), std::back_inserter(vertices),
                           [&dict, &bpmn_to_string](const auto& proto_node) {
                               const auto vertex_id{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_node.node_id())};
                               const auto vertex_type{proto_node_to_vertex(proto_node, &dict)};
                               const auto string_repr{convert_to_string(proto_node)};
                               bpmn_to_string.emplace(vertex_id, string_repr);
                               return cpml::model::bpmn::vertex{vertex_id, vertex_type};
                           });

    // Transform proto edges to internal representation
    cpml::model::bpmn_graph::edge_collection edges{};
    edges.reserve(bpmn_proto.edges().size());
    std::ranges::transform(bpmn_proto.edges(), std::back_inserter(edges), [](const auto& proto_edge) {
        return cpml::model::bpmn::edge{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.from()),
                                       ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.to())};
    });

    return {cpml::model::bpmn_graph::constraint_checked_bpmn_graph(vertices, std::move(edges)),
            std::move(bpmn_to_string)};
}

StatusOr<bpmn_model_description> transform_to_proto_bpmn(const nlohmann::json& json_model) {
    ::celonis::accelerator::BpmnModelDescription proto_bpmn_model_description;
    if (const auto status{
                google::protobuf::util::JsonStringToMessage(json_model.dump(), &proto_bpmn_model_description)};
        !status.ok()) {
        return Status::InvalidArgument(fmt::format("celonis_align_model: Invalid JSON bpmn model description. {}",
                                                   status.error_message().as_string()));
    }

    return bpmn_model_description::from_proto(proto_bpmn_model_description);
}

cpml::model::bpmn_graph transform_to_bpmn_graph(const bpmn_model_description& proto_description) {
    std::vector<cpml::model::bpmn::vertex> vertices{};
    vertices.reserve(proto_description.nodes().size());
    std::ranges::transform(proto_description.nodes(), std::back_inserter(vertices), [](const auto& proto_node) {
        const auto vertex_id{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_node.node_id())};
        const auto vertex_type{proto_node_to_vertex(proto_node, nullptr)};
        return cpml::model::bpmn::vertex{vertex_id, vertex_type};
    });

    // Transform proto edges to internal representation
    cpml::model::bpmn_graph::edge_collection edges{};
    edges.reserve(proto_description.edges().size());
    std::ranges::transform(proto_description.edges(), std::back_inserter(edges), [](const auto& proto_edge) {
        return cpml::model::bpmn::edge{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.from()),
                                       ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.to())};
    });

    return cpml::model::bpmn_graph::constraint_checked_bpmn_graph(vertices, std::move(edges));
}
} // namespace details

StatusOr<cpml::model::bpmn_graph> transform_to_bpmn_graph(const nlohmann::json& json_model) {
    const auto bpmn_proto{details::transform_to_proto_bpmn(json_model)};
    if (!bpmn_proto.ok()) {
        return bpmn_proto.status();
    }
    return details::transform_to_bpmn_graph(bpmn_proto.value());
}
} // namespace starrocks::celonis