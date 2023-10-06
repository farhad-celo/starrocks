#include "bpmn_graph.h"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <ranges>
#include <stack>

#include <boost/graph/adjacency_list.hpp>

#include "ctl/algorithm.h"
#include "ctl/assert.h"
#include "ctl/conversion.h"
#include "ctl/hash.h"
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
  ctl::assert_unreachable();
}

[[nodiscard]] bool is_looping_activity(const process_tree::redo& redo) {
  return !redo.children.empty() && std::holds_alternative<process_tree::activity>(redo.children.front().node) &&
         std::ranges::all_of(
             std::next(std::cbegin(redo.children)), std::cend(redo.children),
             [](const auto& process_tree) { return std::holds_alternative<process_tree::tau>(process_tree.node); });
}

[[nodiscard]] vertex_id_type insert_process_tree(const process_tree& process_tree, vertex_id_type source_vertex,
                                                 bpmn_graph_builder& bldr, object_id object = {});

[[nodiscard]] vertex_id_type insert_looping_activity(const process_tree::activity& activity,
                                                     vertex_id_type source_vertex, cel_int_t loop_count,
                                                     bpmn_graph_builder& bldr, object_id object = {}) {
  const auto child_id{insert_process_tree(process_tree{activity}, source_vertex, bldr, object)};
  bldr.edge(child_id, child_id, object, ctl::cast<cel_int_t>(loop_count));
  return child_id;
}

[[nodiscard]] vertex_id_type insert_process_tree(const process_tree& process_tree, vertex_id_type source_vertex,
                                                 bpmn_graph_builder& bldr, object_id object) {
  return std::visit(
      ctl::overloaded{
          [source_vertex](process_tree::tau /**/) { return source_vertex; },
          [&bldr, source_vertex, object](const process_tree::activity& activity) {
            const auto task_id{bldr.add_vertex(task{activity.activity_id})};
            const auto count{ctl::cast<cel_int_t>(activity.object_count)};
            bldr.edge(source_vertex, task_id, object, count);
            return task_id;
          },
          [&bldr, source_vertex, object](const process_tree::exclusive& exclusive) {
            const auto opening_id{bldr.add_vertex(exclusive_choice{})};
            const auto closing_id{bldr.add_vertex(exclusive_choice{})};
            const auto count{std::accumulate(std::begin(exclusive.child_object_counts),
                                             std::end(exclusive.child_object_counts), cel_int_t{})};
            bldr.edge(source_vertex, opening_id, object, count);
            for (std::size_t i{0}; i != exclusive.children.size(); ++i) {
              const auto last_child_id{insert_process_tree(exclusive.children[i], opening_id, bldr, object)};
              bldr.edge(last_child_id, closing_id, object, ctl::cast<cel_int_t>(exclusive.child_object_counts[i]));
            }
            return closing_id;
          },
          [&bldr, source_vertex, object](const process_tree::parallel& par) {
            const auto opening_id{bldr.add_vertex(parallel{})};
            const auto closing_id{bldr.add_vertex(parallel{})};
            const auto count{ctl::cast<cel_int_t>(par.object_count)};
            bldr.edge(source_vertex, opening_id, object, count);
            for (const auto& child : par.children) {
              const auto last_child_id{insert_process_tree(child, opening_id, bldr, object)};
              bldr.edge(last_child_id, closing_id, object, count);
            }
            return closing_id;
          },
          [&bldr, source_vertex, object](const process_tree::sequence& seq) {
            auto current_source{source_vertex};
            for (const auto& child : seq.children) {
              current_source = insert_process_tree(child, current_source, bldr, object);
            }
            return current_source;
          },
          [&bldr, source_vertex, object](const process_tree::redo& redo) {
            if (redo.children.empty()) {
              return source_vertex;
            }
            if (is_looping_activity(redo)) {
              const auto loop_count{
                  redo.child_redo_counts.front() -
                  redo.object_count};  // child_redo_counts[0] = redo.object_count + child_redo_counts[1...N]
              auto loop_activity{std::get<process_tree::activity>(redo.children.front().node)};
              // From a tree of shape *(A:b, tau:c):a, where a=b-c, we output a BPMN graph of the form:
              //    SOURCE --a--> task_A
              //    task_A --c--> task_A
              //    task_A --a--> TARGET
              //  This is achieved by calling insert_process_tree(task_A). But this sets the count of SOURCE --> task_A
              //    to the count of task_A (in this case, b), which caused a bug (see CPL-7295)
              //    this is why we must set the count of task_A to the count of the loop (redo.object_count)
              //    the activity node is copied to make sure that we don't mess with the original tree
              loop_activity.object_count = redo.object_count;
              return insert_looping_activity(loop_activity, source_vertex, ctl::cast<cel_int_t>(loop_count), bldr,
                                             object);
            }

            const auto opening_id{bldr.add_vertex(exclusive_choice{})};
            const auto closing_id{bldr.add_vertex(exclusive_choice{})};
            const auto object_count{ctl::cast<cel_int_t>(redo.object_count)};
            bldr.edge(source_vertex, closing_id, object, object_count);
            // do part
            const auto do_id{insert_process_tree(redo.children.front(), closing_id, bldr, object)};
            bldr.edge(do_id, opening_id, object, ctl::cast<cel_int_t>(redo.child_redo_counts.front()));
            // redo part
            for (std::size_t i{1}; i != redo.children.size(); ++i) {
              const auto redo_id{insert_process_tree(redo.children[i], opening_id, bldr, object)};
              bldr.edge(redo_id, closing_id, object, ctl::cast<cel_int_t>(redo.child_redo_counts[i]));
            }
            return opening_id;
          }},
      process_tree.node);
}

void insert_process_tree(const process_tree& process_tree, vertex_id_type source_vertex, vertex_id_type target_vertex,
                         bpmn_graph_builder& bldr, object_id object = {}) {
  const auto last_vertex_id{insert_process_tree(process_tree, source_vertex, bldr, object)};
  const auto count{std::visit(ctl::overloaded{[](const process_tree::exclusive& excl) {
                                                return std::accumulate(std::begin(excl.child_object_counts),
                                                                       std::end(excl.child_object_counts),
                                                                       cel_int_t{0});
                                              },
                                              [](const auto& v) { return ctl::cast<cel_int_t>(v.object_count); }},
                              process_tree.node)};
  bldr.edge(last_vertex_id, target_vertex, object, count);
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
      ctl::overloaded{[&](const bpmn::task& /*task*/) { return 0; }, [&](const bpmn::start& /*start*/) { return 1; },
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

[[nodiscard]] bpmn_graph remap_task_ids(const bpmn_graph& graph, const ctl::static_array<row_id>& mapping_vector) {
  bpmn_graph_builder bldr{};
  std::unordered_map<vertex_id_type, vertex_id_type> new_vertex_ids{};

  std::for_each(graph.get_vertices().begin(), graph.get_vertices().end(), [&](const auto& pair) {
    const auto& vertex_type{pair.second.get_vertex_type()};
    if (std::holds_alternative<bpmn::task>(vertex_type)) {
      row_id new_task_id{mapping_vector[std::get<bpmn::task>(vertex_type).activity_id]};
      const auto vertex_id{bldr.add_vertex(bpmn::task{new_task_id})};
      new_vertex_ids[pair.second.get_vertex_id()] = vertex_id;
    } else {
      const auto vertex_id{bldr.add_vertex(vertex_type)};
      new_vertex_ids[pair.second.get_vertex_id()] = vertex_id;
    }
  });

  std::for_each(graph.get_edges().begin(), graph.get_edges().end(), [&](const auto& p) {
    vertex_id_type new_source_id{new_vertex_ids[p.get_source_id()]};
    vertex_id_type new_target_id{new_vertex_ids[p.get_target_id()]};
    bldr.edge(new_source_id, new_target_id, p.get_object_id(), p.get_count());
  });

  return bldr.build();
}

bpmn_graph convert_to_bpmn_graph(const process_tree& process_tree, object_id object) {
  bpmn_graph_builder result{};
  const auto start_id{result.add_vertex(start{})};
  const auto end_id{result.add_vertex(end{})};
  insert_process_tree(process_tree, start_id, end_id, result, object);
  return result.build();
}

bpmn_graph extract_single_object_bpmn_graph(const bpmn_graph& graph, object_id object) {
  debug_assert(!graph.is_single_object(),
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

[[nodiscard]] bpmn_graph overlay_bpmn_graphs(const bpmn_graph& lhs, const bpmn_graph& rhs,
                                             const object_id rhs_object_id) {
  std::unordered_map<row_id, vertex> lhs_tasks_mapped_to_activity_ids{};
  for (const auto& lhs_vertex : lhs.get_vertices()) {
    std::visit(ctl::overloaded{[](const auto& /*vertex_type*/) {},
                               [&lhs_tasks_mapped_to_activity_ids, &lhs_vertex](const task& task) {
                                 lhs_tasks_mapped_to_activity_ids.insert({task.activity_id, lhs_vertex.second});
                               }},
               lhs_vertex.second.get_vertex_type());
  }
  auto bldr{bpmn_graph_builder{}.edges(lhs.get_edges())};
  // TODO(j.kruska) This can lead to collisions
  std::ranges::for_each(lhs.get_vertices(), [&bldr](const auto& pair) { bldr.vertex(pair.second); });
  std::unordered_map<vertex_id_type, vertex_id_type> index_mapping{};
  std::transform(std::cbegin(rhs.get_vertices()), std::cend(rhs.get_vertices()),
                 std::inserter(index_mapping, std::end(index_mapping)),
                 [&bldr, &lhs_tasks_mapped_to_activity_ids](const auto& rhs_vertex) {
                   return std::visit(
                       ctl::overloaded{[&bldr, &rhs_vertex](const auto& v) {
                                         const auto vertex_id{bldr.add_vertex(v)};
                                         return std::make_pair(rhs_vertex.second.get_vertex_id(), vertex_id);
                                       },
                                       [&bldr, &rhs_vertex, &lhs_tasks_mapped_to_activity_ids](const task& task) {
                                         auto lhs_task = lhs_tasks_mapped_to_activity_ids.find(task.activity_id);
                                         if (lhs_task == lhs_tasks_mapped_to_activity_ids.end()) {
                                           const auto vertex_id{bldr.add_vertex({task})};
                                           return std::make_pair(rhs_vertex.second.get_vertex_id(), vertex_id);
                                         }
                                         return std::make_pair(rhs_vertex.second.get_vertex_id(),
                                                               lhs_task->second.get_vertex_id());
                                       }},
                       rhs_vertex.second.get_vertex_type());
                 });

  for (const auto& e : rhs.get_edges()) {
    bldr.edge(edge{index_mapping[e.get_source_id()], index_mapping[e.get_target_id()], rhs_object_id, e.get_count()});
  }
  return bldr.build();
}

bpmn_graph convert_from_proto(const BpmnModelDescription& bpmn_proto, const memory::column_t& activity_column,
                              common::execution_context& operator_context) {
  // Transform proto nodes (i.e., vertices) to internal representation
  std::vector<vertex> vertices{};
  vertices.reserve(ctl::cast_unsigned(bpmn_proto.nodes_size()));
  std::transform(bpmn_proto.nodes().cbegin(), bpmn_proto.nodes().cend(), std::back_inserter(vertices),
                 [&activity_column, &operator_context](const auto& proto_node) {
                   const auto vertex_id{ctl::cast<vertex_id_type>(proto_node.node_id())};
                   const auto vertex_type{proto_node_to_vertex(proto_node, activity_column, operator_context)};
                   return vertex{vertex_id, vertex_type};
                 });

  // Transform proto edges to internal representation
  bpmn_graph::edge_collection edges{};
  edges.reserve(ctl::cast_unsigned(bpmn_proto.edges_size()));
  std::transform(
      bpmn_proto.edges().cbegin(), bpmn_proto.edges().cend(), std::back_inserter(edges), [](const auto& proto_edge) {
        return edge{ctl::cast<vertex_id_type>(proto_edge.from()), ctl::cast<vertex_id_type>(proto_edge.to())};
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
      ctl::assert_unreachable();
  }
}

std::pair<bpmn_graph, bpmn_to_string_t> convert_from_proto_and_create_string_map(
    const BpmnModelDescription& bpmn_proto, const memory::column_t& activity_column,
    common::execution_context& operator_context) {
  bpmn_to_string_t bpmn_to_string;

  // Transform proto nodes (i.e., vertices) to internal representation
  std::vector<vertex> vertices{};
  vertices.reserve(ctl::cast_unsigned(bpmn_proto.nodes_size()));
  std::transform(bpmn_proto.nodes().cbegin(), bpmn_proto.nodes().cend(), std::back_inserter(vertices),
                 [&activity_column, &operator_context, &bpmn_to_string](const auto& proto_node) {
                   const auto vertex_id{ctl::cast<vertex_id_type>(proto_node.node_id())};
                   const auto vertex_type{proto_node_to_vertex(proto_node, activity_column, operator_context)};
                   const auto string_repr{convert_to_string(proto_node)};
                   bpmn_to_string.emplace(vertex_id, string_repr);
                   return vertex{vertex_id, vertex_type};
                 });

  // Transform proto edges to internal representation
  bpmn_graph::edge_collection edges{};
  edges.reserve(ctl::cast_unsigned(bpmn_proto.edges_size()));
  std::transform(
      bpmn_proto.edges().cbegin(), bpmn_proto.edges().cend(), std::back_inserter(edges), [](const auto& proto_edge) {
        return edge{ctl::cast<vertex_id_type>(proto_edge.from()), ctl::cast<vertex_id_type>(proto_edge.to())};
      });

  return {bpmn_graph{vertices, edges}, bpmn_to_string};
}

std::string to_string(const bpmn_graph& model) {
  std::ostringstream strm{};
  static constexpr char NEW_LINE{'\n'};
  strm << "=== NODES ===" << NEW_LINE;
  for (const auto& [_, v] : model.get_vertices()) {
    strm << fmt::format("{}: {}", v.get_vertex_id(), to_string(v.get_vertex_type())) << NEW_LINE;
  }

  strm << "=== EDGES ===" << NEW_LINE;
  for (const auto& e : model.get_edges()) {
    // TODO(n.weber): Improve formatting (comma separation, new lines, ...)
    strm << fmt::format("{}->{}", e.get_source_id(), e.get_target_id());
  }
  strm << NEW_LINE;

  strm << "=== ACTIVITY MAPPING ===" << NEW_LINE;
  for (const auto& [activity_id, node_id] : model.activity_id_to_vertex_id()) {
    strm << fmt::format("{} => {}", activity_id, node_id) << NEW_LINE;
  }

  return strm.str();
}

std::ostream& operator<<(std::ostream& os, const bpmn_graph& graph) {
  os << to_string(graph);
  return os;
}

const vertex& bpmn_graph::get_vertex(vertex_id_type id) const {
  debug_assert(vertices_.at(id).get_vertex_id() == id);
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
  debug_assert(start_vertex_ids().size() == 1);
  return start_vertex_ids().at(0);
}

vertex_id_type bpmn_graph::single_end_vertex() const noexcept {
  debug_assert(end_vertex_ids().size() == 1);
  return end_vertex_ids().at(0);
}

bool bpmn_graph::is_single_object() const {
  return get_cached_or_calculate(caches_.is_single_object, [this]() {
    return std::ranges::all_of(edges_,
                               [this](const edge& e) { return e.get_object_id() == edges_.at(0).get_object_id(); });
  });
}

}  // namespace celonis::accelerator::operators::process::bpmn
