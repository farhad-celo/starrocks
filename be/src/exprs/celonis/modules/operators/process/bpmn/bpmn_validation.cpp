#include "bpmn_validation.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <ranges>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "legacy_embedded_ctl/algorithm.h"
#include "legacy_embedded_ctl/assert.h"
#include "modules/common/exceptions.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/edge.h"
#include "modules/operators/process/bpmn/vertex.h"
#include "modules/operators/process/bpmn/vertex_types.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

using problems_t = std::vector<std::string>;

problems_t validate_all_tasks_single_inout(const bpmn_graph& graph, problems_t problems) {
  std::unordered_map<object_id, std::unordered_set<vertex_id_type>> ingoing{};
  std::unordered_map<object_id, std::unordered_set<vertex_id_type>> outgoing{};
  std::unordered_map<object_id, std::unordered_set<vertex_id_type>> selfloops{};
  for (const auto& e : graph.get_edges()) {
    const bool is_self_loop{e.get_source_id() == e.get_target_id()};
    if (is_self_loop && is_task(graph.get_vertex(e.get_source_id()))) {
      if (!selfloops[e.get_object_id()].emplace(e.get_source_id()).second) {
        problems.emplace_back(
            fmt::format("Error [{}] already has a self-loop for object {}.", e.get_source_id(), e.get_object_id()));
      }
    }
    if (!is_self_loop && is_task(graph.get_vertex(e.get_source_id()))) {
      if (!outgoing[e.get_object_id()].emplace(e.get_source_id()).second) {
        problems.emplace_back(fmt::format("Error for edge [{}->{}]. [{}] already has an outgoing edge for object {}.",
                                          e.get_source_id(), e.get_target_id(), e.get_source_id(), e.get_object_id()));
      }
    }
    if (!is_self_loop && is_task(graph.get_vertex(e.get_target_id()))) {
      if (!ingoing[e.get_object_id()].emplace(e.get_target_id()).second) {
        problems.emplace_back(fmt::format("Error for edge [{}->{}]. [{}] already has an ingoing edge for object {}.",
                                          e.get_source_id(), e.get_target_id(), e.get_target_id(), e.get_object_id()));
      }
    }
  }
  return problems;
}

problems_t validate_no_duplicate_activities(const bpmn_graph& graph, problems_t problems) {
  std::unordered_map<row_id, vertex_id_type> activities{};
  for (const auto& [_, v] : graph.get_vertices()) {
    if (const auto& type{v.get_vertex_type()}; is_task(type)) {
      const auto activity_id{std::get<task>(type).activity_id};
      if (activity_id == VALUE_NOT_FOUND) {
        continue;
      }
      if (const auto elem{activities.try_emplace(activity_id, v.get_vertex_id())}; !elem.second) {
        problems.emplace_back(fmt::format(
            "Encountered duplicate activities. The task of the vertex [{}] to add has the activity ID [{}] which is "
            "already mapped to a vertex with ID [{}] (existing).",
            v.get_vertex_id(), activity_id, elem.first->second));
      }
    }
  }
  return problems;
}

/** @brief Validates that the edge to add does refer to existing vertices */
std::pair<problems_t, bool> validate_edge_refers_to_valid_vertices(const edge& e, const bpmn_graph& graph,
                                                                   problems_t problems) {
  bool result{true};
  if (!graph.get_vertices().contains(e.get_source_id())) {
    problems.emplace_back(fmt::format("Error for edge [{}->{}]. Vertex with ID [{}] does not exist.", e.get_source_id(),
                                      e.get_target_id(), e.get_source_id()));
    result = false;
  }
  if (!graph.get_vertices().contains(e.get_target_id())) {
    problems.emplace_back(fmt::format("Error for edge [{}->{}]. Vertex with ID [{}] does not exist.", e.get_source_id(),
                                      e.get_target_id(), e.get_target_id()));
    result = false;
  }
  return {std::move(problems), result};
}

/** @brief Validates that there are no edges with the same source and target belonging to the same object. */
problems_t validate_no_duplicate_edges(const bpmn_graph& graph, problems_t problems) {
  std::unordered_set<edge, edge_hash_ignore_count, edge_equal_to_ignore_count>
      contained_edges;  // For duplicate edges we want to ignore the counts
  for (const auto& e : graph.get_edges()) {
    if (contained_edges.contains(e)) {
      problems.emplace_back(fmt::format("Encountered duplicate edge [{}->{}] for object [{}].", e.get_source_id(),
                                        e.get_target_id(), e.get_object_id()));
    }
    contained_edges.emplace(e);
  }
  return problems;
}

/** @brief Validates a given edge does conform to the BPMN specification (e.g., no outgoing edge from an end node). */
problems_t validate_edge_conforms_to_bpmn_spec(const edge& e, const bpmn_graph& graph, problems_t problems) {
  if (const auto type{graph.get_vertex(e.get_source_id()).get_vertex_type()}; is_end(type)) {
    problems.emplace_back(
        fmt::format("Error for edge [{}->{}]. Vertex with ID [{}] is a [{}] node which is not allowed to have "
                    "outgoing edges.",
                    e.get_source_id(), e.get_target_id(), e.get_source_id(), to_string(type)));
  }
  if (const auto type{graph.get_vertex(e.get_target_id()).get_vertex_type()}; is_start(type)) {
    problems.emplace_back(
        fmt::format("Error for edge [{}->{}]. Vertex with ID [{}] is a [{}] node which is not allowed to have "
                    "ingoing edges.",
                    e.get_source_id(), e.get_target_id(), e.get_target_id(), to_string(type)));
  }
  if (const auto source{e.get_source_id()}; source == e.get_target_id() && !is_task(graph.get_vertex(source))) {
    problems.emplace_back(fmt::format(
        "Error for edge [{}->{}]. Vertex with ID [{}] is a [{}] node which is not allowed to have self-loops.",
        e.get_source_id(), e.get_target_id(), e.get_target_id(),
        to_string(graph.get_vertex(source).get_vertex_type())));
  }
  return problems;
}

problems_t validate_start_single_outgoing(const bpmn_graph& graph, const vertex& v, problems_t problems) {
  if (is_start(v) && graph.get_outgoing_edges(v).size() != 1) {
    problems.emplace_back(fmt::format("Start vertex [{}] does not have a unique outgoing edge.", v.get_vertex_id()));
  }
  return problems;
}

problems_t validate_end_single_ingoing(const bpmn_graph& graph, const vertex& v, problems_t problems) {
  if (is_end(v) && graph.get_ingoing_edges(v).size() != 1) {
    problems.emplace_back(fmt::format("End vertex [{}] does not have a unique ingoing edge.", v.get_vertex_id()));
  }
  return problems;
}

/**
 * Finds all nodes that are reachable from the given nodes by following the specified edges.
 * @param initial_node the node id of the start node
 * @param edges the list of edges of the bpmn model. Either ingoing_edges or outgoing_edges.
 * @return set indicating nodes that are reachable from initial_node.
 */
std::unordered_set<vertex_id_type> find_reachable_nodes(
    bpmn_graph::vertex_ids initial_nodes, const std::unordered_map<vertex_id_type, bpmn_graph::vertex_ids>& edges) {
  std::unordered_set<vertex_id_type> visited{};

  while (!initial_nodes.empty()) {
    const auto current_node{initial_nodes.back()};
    initial_nodes.pop_back();
    visited.insert(current_node);
    std::ranges::copy_if(edges.at(current_node), std::back_inserter(initial_nodes),
                         [&visited](const auto& next_node) { return !legacy_embedded_ctl::contains(visited, next_node); });
  }

  return visited;
}

/** Validates that all nodes are on a path from start to end */
problems_t validate_single_object_nodes_on_path(const bpmn_graph& bpmn_graph, problems_t problems) {
  legacy_embedded_debug_assert(bpmn_graph.is_single_object());
  const auto object{bpmn_graph.get_edges().at(0).get_object_id()};
  const auto reachable_from_start{find_reachable_nodes(bpmn_graph.start_vertex_ids(), bpmn_graph.outgoing_vertices())};
  const auto reachable_from_end{find_reachable_nodes(bpmn_graph.end_vertex_ids(), bpmn_graph.ingoing_vertices())};

  if (bpmn_graph.get_vertices().size() != reachable_from_start.size() ||
      bpmn_graph.get_vertices().size() != reachable_from_end.size()) {
    for (const auto& [_, v] : bpmn_graph.get_vertices()) {
      const auto node_id{v.get_vertex_id()};
      if (!legacy_embedded_ctl::contains(reachable_from_start, node_id)) {
        problems.emplace_back(
            fmt::format("Node with ID [{}] is not reachable from the start node of object {}.", node_id, object));
      }
      if (!legacy_embedded_ctl::contains(reachable_from_end, node_id)) {
        problems.emplace_back(
            fmt::format("Node with ID [{}] is not reachable from the end node of object {}.", node_id, object));
      }
    }
  }
  return problems;
}

/** Validates that all nodes are on a path from start to end */
problems_t validate_nodes_on_path(const bpmn_graph& bpmn_graph, problems_t problems) {
  std::unordered_set<object_id> objects;
  for (const auto& e : bpmn_graph.get_edges()) {
    objects.emplace(e.get_object_id());
  }

  if (objects.size() != bpmn_graph.start_vertex_ids().size()) {
    problems.emplace_back(
        fmt::format("There are {} objects but {} start nodes.", objects.size(), bpmn_graph.start_vertex_ids().size()));
  }

  if (objects.size() != bpmn_graph.end_vertex_ids().size()) {
    problems.emplace_back(
        fmt::format("There are {} objects but {} start nodes.", objects.size(), bpmn_graph.end_vertex_ids().size()));
  }

  if (objects.size() == 1) {
    problems = validate_single_object_nodes_on_path(bpmn_graph, std::move(problems));
  } else {
    for (const auto object : objects) {
      const auto subgraph{extract_single_object_bpmn_graph(bpmn_graph, object)};
      problems = validate_single_object_nodes_on_path(subgraph, std::move(problems));
    }
  }
  return problems;
}

}  // anonymous namespace

void validate_bpmn_model_consistency(const bpmn_graph& bpmn_model) {
  const auto report_problems_if_necessary{[](const problems_t& problems) {
    if (!problems.empty()) {
      throw common::cpm_exception{"BPMN model is invalid. Problems: {}", fmt::join(problems, " ")};
    }
  }};

  problems_t problems{};

  // Vertex validators
  problems = validate_no_duplicate_activities(bpmn_model, std::move(problems));

  // Edge validators
  problems = validate_no_duplicate_edges(bpmn_model, std::move(problems));
  for (const auto& e : bpmn_model.get_edges()) {
    auto pair{validate_edge_refers_to_valid_vertices(e, bpmn_model, std::move(problems))};
    problems = std::move(pair.first);
    if (pair.second) {
      problems = validate_edge_conforms_to_bpmn_spec(e, bpmn_model, std::move(problems));
    };
  }

  // if we have encountered problems, e.g. there are edges to non-existent vertices, we might not be able to check
  // the rest
  report_problems_if_necessary(problems);

  for (const auto& [_, v] : bpmn_model.get_vertices()) {
    problems = validate_start_single_outgoing(bpmn_model, v, std::move(problems));
    problems = validate_end_single_ingoing(bpmn_model, v, std::move(problems));
  }

  report_problems_if_necessary(problems);
}

void validate_bpmn_model_constraints(const bpmn_graph& bpmn_model) {
  problems_t problems{};

  problems = validate_nodes_on_path(bpmn_model, std::move(problems));
  problems = validate_all_tasks_single_inout(bpmn_model, std::move(problems));

  if (!problems.empty()) {
    throw common::cpm_exception{"BPMN model is invalid. Problems: {}", fmt::join(problems, " ")};
  }
}
}  // namespace celonis::accelerator::operators::process::bpmn