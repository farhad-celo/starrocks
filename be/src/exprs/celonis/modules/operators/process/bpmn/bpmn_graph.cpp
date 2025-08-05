#include "bpmn_graph.h"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <ranges>
#include <stack>

#include <boost/graph/adjacency_list.hpp>

#include "legacy_embedded_ctl/algorithm.h"
#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/hash.h"
#include "modules/memory/column.h"
#include "modules/operators/process/bpmn/bpmn_graph_builder.h"
#include "modules/operators/process/bpmn/bpmn_validation.h"
#include "modules/query/operators.pb.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

template <typename T, typename FUNCTION>
const T& get_cached_or_calculate(std::optional<T>& cache, FUNCTION&& calculate) {
  if (!cache.has_value()) {
    cache = calculate();
  }
  return cache.value();
}

[[nodiscard]] vertex_type proto_node_to_vertex(const BpmnModelDescription::BpmnNode& proto_bpmn_node,
                                               const memory::column_t& activity_column,
                                               common::execution_context& operator_context) {
  using proto_bpmn_node_t = BpmnModelDescription::BpmnNode;
  switch (proto_bpmn_node.node_type()) {
    case proto_bpmn_node_t::TASK:
      return task{activity_column->get_string_dict(operator_context)
                      ->get_row_id_for(proto_bpmn_node.task_name(), operator_context)};
    case proto_bpmn_node_t::EXCLUSIVE_CHOICE:
      return exclusive_choice{};
    case proto_bpmn_node_t::PARALLEL:
      return parallel{};
    case proto_bpmn_node_t::START:
      return start{};
    case proto_bpmn_node_t::END:
      return end{};
  }
  legacy_embedded_ctl::assert_unreachable();
}

}  // namespace

bpmn_graph::bpmn_graph(const std::initializer_list<vertex> vertices, const std::initializer_list<edge> edges)
    : bpmn_graph(std::vector<vertex>(vertices), edge_collection(edges)) {}

bpmn_graph::bpmn_graph(const std::vector<vertex>& vertices, edge_collection edges) : edges_{std::move(edges)} {
  std::ranges::for_each(vertices, [&](const auto& v) {
    if (!vertices_.try_emplace(v.get_vertex_id(), v).second) {
      throw common::cpm_exception{"Encountered duplicate vertex [{}]", v.get_vertex_id()};
    };
  });
  validate_bpmn_model_consistency(*this);
}

// Note that changing any of these values with change the node type outputs of the MO_BPMN_GRAPH operator and will thus
// change the contract with the front end
[[nodiscard]] cel_int_t convert_vertex_type_to_int(const vertex_type& type) {
  return std::visit(
      legacy_embedded_ctl::overloaded{[&](const bpmn::task& /*task*/) { return 0; }, [&](const bpmn::start& /*start*/) { return 1; },
                      [&](const bpmn::end& /*end*/) { return 2; },
                      [&](const bpmn::exclusive_choice& /*exclusive*/) { return 3; },
                      [&](const bpmn::parallel& /*parallel*/) { return 4; }},
      type);
}

[[nodiscard]] vertex_type convert_int_to_vertex_type(cel_int_t type_int) {
  switch (type_int) {
    case 0:
      return bpmn::task{};
    case 1:
      return bpmn::start{};
    case 2:
      return bpmn::end{};
    case 3:
      return bpmn::exclusive_choice{};
    case 4:
      return bpmn::parallel{};
    default:
      throw common::cpm_exception{"Invalid BPMN type mapping [{}]", type_int};
  }
}

bpmn_graph extract_single_object_bpmn_graph(const bpmn_graph& graph, object_id object) {
  legacy_embedded_debug_assert(!graph.is_single_object(),
               "Extracting single object subgraph from a graph that is already single object.");
  bpmn_graph_builder bldr{};
  std::unordered_map<vertex_id_type, vertex_id_type> vertex_mapping{};
  auto find_mapped_vertex{[&bldr, &vertex_mapping](const vertex& v) {
    if (!vertex_mapping.contains(v.get_vertex_id())) {
      return vertex_mapping.emplace(v.get_vertex_id(), bldr.add_vertex(v.get_vertex_type())).first->second;
    }
    return vertex_mapping.at(v.get_vertex_id());
  }};
  for (const auto& e : graph.get_edges()) {
    if (e.get_object_id() == object) {
      auto source{find_mapped_vertex(graph.get_vertex(e.get_source_id()))};
      auto target{find_mapped_vertex(graph.get_vertex(e.get_target_id()))};
      bldr.edge(source, target, e.get_object_id(), e.get_count());
    }
  }
  return bldr.build();
}

bpmn_graph convert_from_proto(const BpmnModelDescription& bpmn_proto, const memory::column_t& activity_column,
                              common::execution_context& operator_context) {
  // Transform proto nodes (i.e., vertices) to internal representation
  std::vector<vertex> vertices{};
  vertices.reserve(legacy_embedded_ctl::cast_unsigned(bpmn_proto.nodes_size()));
  std::transform(bpmn_proto.nodes().cbegin(), bpmn_proto.nodes().cend(), std::back_inserter(vertices),
                 [&activity_column, &operator_context](const auto& proto_node) {
                   const auto vertex_id{legacy_embedded_ctl::cast<vertex_id_type>(proto_node.node_id())};
                   const auto vertex_type{proto_node_to_vertex(proto_node, activity_column, operator_context)};
                   return vertex{vertex_id, vertex_type};
                 });

  // Transform proto edges to internal representation
  bpmn_graph::edge_collection edges{};
  edges.reserve(legacy_embedded_ctl::cast_unsigned(bpmn_proto.edges_size()));
  std::transform(
      bpmn_proto.edges().cbegin(), bpmn_proto.edges().cend(), std::back_inserter(edges), [](const auto& proto_edge) {
        return edge{legacy_embedded_ctl::cast<vertex_id_type>(proto_edge.from()), legacy_embedded_ctl::cast<vertex_id_type>(proto_edge.to())};
      });

  return bpmn_graph{vertices, edges};
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
      legacy_embedded_ctl::assert_unreachable();
  }
}

std::pair<bpmn_graph, bpmn_to_string_t> convert_from_proto_and_create_string_map(
    const BpmnModelDescription& bpmn_proto, const memory::column_t& activity_column,
    common::execution_context& operator_context) {
  bpmn_to_string_t bpmn_to_string;

  // Transform proto nodes (i.e., vertices) to internal representation
  std::vector<vertex> vertices{};
  vertices.reserve(legacy_embedded_ctl::cast_unsigned(bpmn_proto.nodes_size()));
  std::transform(bpmn_proto.nodes().cbegin(), bpmn_proto.nodes().cend(), std::back_inserter(vertices),
                 [&activity_column, &operator_context, &bpmn_to_string](const auto& proto_node) {
                   const auto vertex_id{legacy_embedded_ctl::cast<vertex_id_type>(proto_node.node_id())};
                   const auto vertex_type{proto_node_to_vertex(proto_node, activity_column, operator_context)};
                   const auto string_repr{convert_to_string(proto_node)};
                   bpmn_to_string.emplace(vertex_id, string_repr);
                   return vertex{vertex_id, vertex_type};
                 });

  // Transform proto edges to internal representation
  bpmn_graph::edge_collection edges{};
  edges.reserve(legacy_embedded_ctl::cast_unsigned(bpmn_proto.edges_size()));
  std::transform(
      bpmn_proto.edges().cbegin(), bpmn_proto.edges().cend(), std::back_inserter(edges), [](const auto& proto_edge) {
        return edge{legacy_embedded_ctl::cast<vertex_id_type>(proto_edge.from()), legacy_embedded_ctl::cast<vertex_id_type>(proto_edge.to())};
      });

  return {bpmn_graph{vertices, edges}, bpmn_to_string};
}

std::string to_string(const bpmn_graph& model) {
  std::ostringstream strm{};
  static constexpr char NEW_LINE{'\n'};
  strm << "=== NODES ===" << NEW_LINE;
  for (const auto& [_, v] : model.get_vertices()) {
    strm << fmt::format("[{}: {}]", v.get_vertex_id(), to_string(v.get_vertex_type())) << NEW_LINE;
  }

  strm << "=== EDGES ===" << NEW_LINE;
  for (const auto& e : model.get_edges()) {
    strm << fmt::format("[{}->{}] ", e.get_source_id(), e.get_target_id());
  }
  strm << NEW_LINE;

  strm << "=== ACTIVITY MAPPING ===" << NEW_LINE;
  for (const auto& [activity_id, node_id] : model.activity_id_to_vertex_id()) {
    strm << fmt::format("[{} => {}]", activity_id, node_id) << NEW_LINE;
  }

  return strm.str();
}

std::ostream& operator<<(std::ostream& os, const bpmn_graph& graph) {
  os << to_string(graph);
  return os;
}

const vertex& bpmn_graph::get_vertex(vertex_id_type id) const {
  legacy_embedded_debug_assert(vertices_.at(id).get_vertex_id() == id);
  return vertices_.at(id);
}

const bpmn_graph::adjacent_edge_map& bpmn_graph::get_outgoing_edges() const {
  return get_cached_or_calculate(caches_.outgoing_edges, [this]() {
    adjacent_edge_map result(vertices_.size());
    for (const auto& [key, _] : vertices_) {
      result.emplace(key, edge_collection{});
    }
    for (const auto& e : edges_) {
      result.at(e.get_source_id()).emplace_back(e);
    }
    return result;
  });
}

const bpmn_graph::adjacent_edge_map& bpmn_graph::get_ingoing_edges() const {
  return get_cached_or_calculate(caches_.ingoing_edges, [this]() {
    adjacent_edge_map result{};
    for (const auto& [key, _] : vertices_) {
      result.emplace(key, edge_collection{});
    }
    for (const auto& e : edges_) {
      result.at(e.get_target_id()).emplace_back(e);
    }
    return result;
  });
}

const bpmn_graph::vertex_to_activity_ptr_t& bpmn_graph::get_vertex_to_activity_mapping() const {
  return get_cached_or_calculate(caches_.vertex_to_activity_mapping, [this]() {
    vertex_to_activity_ptr_t result(vertices_.size());
    std::ranges::for_each(vertices_, [&result](const auto& pair) {
      if (const auto& type{pair.second.get_vertex_type()}; is_task(type)) {
        result.emplace(pair.first, std::get<process::bpmn::task>(type).activity_id);
      }
      result.emplace(pair.first, VALUE_NOT_FOUND);
    });
    return result;
  });
}

const std::unordered_map<row_id, vertex_id_type>& bpmn_graph::activity_id_to_vertex_id() const {
  return get_cached_or_calculate(caches_.activity_id_to_vertex_id, [this]() {
    std::unordered_map<row_id, vertex_id_type> result{};
    for (const auto& [_, v] : vertices_) {
      const auto& type{v.get_vertex_type()};
      if (is_task(type)) {
        result.emplace(std::get<process::bpmn::task>(type).activity_id, v.get_vertex_id());
      }
    }
    return result;
  });
}

const bpmn_graph::adjacent_vertex_map& bpmn_graph::outgoing_vertices() const {
  return get_cached_or_calculate(caches_.outgoing_vertices, [this]() {
    adjacent_vertex_map result(vertices_.size());
    for (const auto& [key, _] : vertices_) {
      result.emplace(key, vertex_ids{});
    }
    for (const auto& e : edges_) {
      result.at(e.get_source_id()).emplace_back(e.get_target_id());
    }
    return result;
  });
}

const bpmn_graph::adjacent_vertex_map& bpmn_graph::ingoing_vertices() const {
  return get_cached_or_calculate(caches_.ingoing_vertices, [this]() {
    adjacent_vertex_map result(vertices_.size());
    for (const auto& [key, _] : vertices_) {
      result.emplace(key, vertex_ids{});
    }
    for (const auto& e : edges_) {
      result.at(e.get_target_id()).emplace_back(e.get_source_id());
    }
    return result;
  });
}

const std::vector<vertex_id_type>& bpmn_graph::start_vertex_ids() const {
  return get_cached_or_calculate(caches_.start_vertices, [this]() {
    std::vector<vertex_id_type> result{};
    std::ranges::for_each(vertices_, [&result](const auto& pair) {
      if (is_start(pair.second)) {
        result.emplace_back(pair.second.get_vertex_id());
      }
    });
    return result;
  });
}
const std::vector<vertex_id_type>& bpmn_graph::end_vertex_ids() const {
  return get_cached_or_calculate(caches_.end_vertices, [this]() {
    std::vector<vertex_id_type> result{};
    std::ranges::for_each(vertices_, [&result](const auto& pair) {
      if (is_end(pair.second)) {
        result.emplace_back(pair.second.get_vertex_id());
      }
    });
    return result;
  });
}

vertex_id_type bpmn_graph::single_start_vertex() const noexcept {
  legacy_embedded_debug_assert(start_vertex_ids().size() == 1);
  return start_vertex_ids().at(0);
}

vertex_id_type bpmn_graph::single_end_vertex() const noexcept {
  legacy_embedded_debug_assert(end_vertex_ids().size() == 1);
  return end_vertex_ids().at(0);
}

bool bpmn_graph::is_single_object() const {
  return get_cached_or_calculate(caches_.is_single_object, [this]() {
    return std::ranges::all_of(edges_,
                               [this](const edge& e) { return e.get_object_id() == edges_.at(0).get_object_id(); });
  });
}

}  // namespace celonis::accelerator::operators::process::bpmn
