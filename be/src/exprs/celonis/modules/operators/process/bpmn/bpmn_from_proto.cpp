#include "bpmn_from_proto.h"

#include <cpml/exception.h>
#include <ctl/conversion.h>

#include "modules/common/remap_exceptions.h"
#include "modules/memory/column.h"
#include "modules/query/operators.pb.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

[[nodiscard]] cpml::model::bpmn::vertex_type proto_node_to_vertex(const BpmnModelDescription::BpmnNode& proto_bpmn_node,
                                                                  const memory::column_t& activity_column,
                                                                  common::execution_context& operator_context) {
  using proto_bpmn_node_t = BpmnModelDescription::BpmnNode;
  switch (proto_bpmn_node.node_type()) {
    case proto_bpmn_node_t::TASK:
      return cpml::model::bpmn::task{
          ctl::cast<cpml::activity_id_t>(activity_column->get_string_dict(operator_context, no_dictify_request{})
                                             ->get_row_id_for(proto_bpmn_node.task_name(), operator_context))};
    case proto_bpmn_node_t::EXCLUSIVE_CHOICE:
      return cpml::model::bpmn::exclusive_choice{};
    case proto_bpmn_node_t::PARALLEL:
      return cpml::model::bpmn::parallel{};
    case proto_bpmn_node_t::START:
      return cpml::model::bpmn::start{};
    case proto_bpmn_node_t::END:
      return cpml::model::bpmn::end{};
  }
  ctl::assert_unreachable();
}

template <typename FUNCTOR>
decltype(auto) map_cpml_exception_to_cpm_exception(FUNCTOR func) {
  try {
    return func();
  } catch (const cpml::invalid_argument& exception) {
    throw common::remap_to_cpm_exception(exception);
  }
}

}  // anonymous namespace

cpml::model::bpmn_graph convert_from_proto(const BpmnModelDescription& bpmn_proto,
                                           const memory::column_t& activity_column,
                                           common::execution_context& operator_context) {
  // Transform proto nodes (i.e., vertices) to internal representation
  std::vector<cpml::model::bpmn::vertex> vertices{};
  vertices.reserve(ctl::cast_unsigned(bpmn_proto.nodes_size()));
  std::transform(bpmn_proto.nodes().cbegin(), bpmn_proto.nodes().cend(), std::back_inserter(vertices),
                 [&activity_column, &operator_context](const auto& proto_node) {
                   const auto vertex_id{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_node.node_id())};
                   const auto vertex_type{proto_node_to_vertex(proto_node, activity_column, operator_context)};
                   return cpml::model::bpmn::vertex{vertex_id, vertex_type};
                 });

  // Transform proto edges to internal representation
  cpml::model::bpmn_graph::edge_collection edges{};
  edges.reserve(ctl::cast_unsigned(bpmn_proto.edges_size()));
  std::transform(bpmn_proto.edges().cbegin(), bpmn_proto.edges().cend(), std::back_inserter(edges),
                 [](const auto& proto_edge) {
                   return cpml::model::bpmn::edge{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.from()),
                                                  ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.to())};
                 });

  // for user provided models, we want to check if they satisfy reachability constraints
  return map_cpml_exception_to_cpm_exception([&vertices, edges = std::move(edges)]() mutable {
    return cpml::model::bpmn_graph::constraint_checked_bpmn_graph(vertices, std::move(edges));
  });
}

[[nodiscard]] std::string convert_to_string(const BpmnModelDescription::BpmnNode& proto_node) {
  switch (proto_node.node_type()) {
    case BpmnModelDescription_BpmnNode_BpmnNodeType_TASK:
      return proto_node.task_name();
    case BpmnModelDescription_BpmnNode_BpmnNodeType_EXCLUSIVE_CHOICE:
      return "BPMN_EXCLUSIVE_CHOICE";
    case BpmnModelDescription_BpmnNode_BpmnNodeType_PARALLEL:
      return "BPMN_PARALLEL";
    case BpmnModelDescription_BpmnNode_BpmnNodeType_START:
      return "BPMN_START";
    case BpmnModelDescription_BpmnNode_BpmnNodeType_END:
      return "BPMN_END";
    default:
      ctl::assert_unreachable();
  }
}

std::pair<cpml::model::bpmn_graph, bpmn_to_string_t> convert_from_proto_and_create_string_map(
    const BpmnModelDescription& bpmn_proto, const memory::column_t& activity_column,
    common::execution_context& operator_context) {
  bpmn_to_string_t bpmn_to_string;

  // Transform proto nodes (i.e., vertices) to internal representation
  std::vector<cpml::model::bpmn::vertex> vertices{};
  vertices.reserve(ctl::cast_unsigned(bpmn_proto.nodes_size()));
  std::transform(bpmn_proto.nodes().cbegin(), bpmn_proto.nodes().cend(), std::back_inserter(vertices),
                 [&activity_column, &operator_context, &bpmn_to_string](const auto& proto_node) {
                   const auto vertex_id{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_node.node_id())};
                   const auto vertex_type{proto_node_to_vertex(proto_node, activity_column, operator_context)};
                   const auto string_repr{convert_to_string(proto_node)};
                   bpmn_to_string.emplace(vertex_id, string_repr);
                   return cpml::model::bpmn::vertex{vertex_id, vertex_type};
                 });

  // Transform proto edges to internal representation
  cpml::model::bpmn_graph::edge_collection edges{};
  edges.reserve(ctl::cast_unsigned(bpmn_proto.edges_size()));
  std::transform(bpmn_proto.edges().cbegin(), bpmn_proto.edges().cend(), std::back_inserter(edges),
                 [](const auto& proto_edge) {
                   return cpml::model::bpmn::edge{ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.from()),
                                                  ctl::cast<cpml::model::bpmn::vertex_id_type>(proto_edge.to())};
                 });

  auto graph{map_cpml_exception_to_cpm_exception([&vertices, edges = std::move(edges)]() mutable {
    // for user provided models, we want to check if they satisfy reachability constraints
    return cpml::model::bpmn_graph::constraint_checked_bpmn_graph(vertices, std::move(edges));
  })};

  return {std::move(graph), std::move(bpmn_to_string)};
}

}  // namespace celonis::accelerator::operators::process::bpmn
