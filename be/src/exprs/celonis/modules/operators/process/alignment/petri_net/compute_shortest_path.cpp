#include "compute_shortest_path.h"

#include "ctl/conversion.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

shortest_paths_matrix::shortest_paths_matrix(size_t number_of_transitions)
    : distances_(number_of_transitions * number_of_transitions, shortest_paths_matrix::UNCONNECTED),
      paths_(number_of_transitions * number_of_transitions, std::vector<petri_net_transition_id>()),
      number_of_transitions_{number_of_transitions} {}

int32_t shortest_paths_matrix::get_distance(petri_net_transition_id transition_from,
                                            petri_net_transition_id transition_to) const {
  const auto index{number_of_transitions_ * transition_from.id + transition_to.id};
  return distances_.at(index);
}

void shortest_paths_matrix::update_distance(petri_net_transition_id transition_from,
                                            petri_net_transition_id transition_to, int32_t new_distance) {
  const auto index{number_of_transitions_ * transition_from.id + transition_to.id};
  distances_.at(index) = new_distance;
}

const std::vector<petri_net_transition_id>& shortest_paths_matrix::get_path(petri_net_transition_id from,
                                                                            petri_net_transition_id to) const {
  const auto index{number_of_transitions_ * from.id + to.id};
  return paths_.at(index);
}

void shortest_paths_matrix::update_path(petri_net_transition_id from, petri_net_transition_id to,
                                        std::vector<petri_net_transition_id> new_path) {
  const auto index{number_of_transitions_ * from.id + to.id};
  paths_.at(index) = std::move(new_path);
}

namespace {

/** Set distance between consecutive transitions = 1, distance to self = 0 */
void set_initial_distances(shortest_paths_matrix& shortest_paths, petri_net_accessor& pn_accessor) {
  for (const auto& pn_transition_u : pn_accessor.get_transitions()) {
    const shortest_paths_matrix::distance_type weight_u{
        string_to_int_mapper::is_tau_transition(pn_transition_u.label) ? 0 : 1};
    const auto transition_u{pn_transition_u.transition};
    // NB we're "fixing" self paths in the end, but until then, we set the distance to 0
    shortest_paths.update_distance(transition_u, transition_u, 0);
    for (const auto& transitions_w : pn_accessor.consecutive_transitions(pn_transition_u)) {
      shortest_paths.update_distance(transition_u, transitions_w, weight_u);
      shortest_paths.update_path(transition_u, transitions_w, {transitions_w});
    }
  }
}

/** Set distance between fork-join nodes to be the distance of entire parallel section */
// Possible improvement: BFS was already computed when computing parallel sections
void set_parallel_section_distances(shortest_paths_matrix& shortest_paths,
                                    petri_net_accessor_with_parallel_sections& pn,
                                    const common::execution_context& context) {
  for (const auto& [from, to] : pn.par_sections().sections()) {
    // check if `to` is a NULL transition (this can happen), and skip the loop in this case
    if (to.is_null()) {
      continue;
    }
    const auto from_marking{pn.accessor().get_marking(pn.accessor()[from].out_places)};

    // TODO (goulart.e) get max bfs depth from config
    petri_net_bfs pn_bfs{30, context};
    const auto transitions{pn_bfs.path_to_transition(pn.accessor(), from_marking, to)};

    const auto is_labelled{[&pn](auto transition) {
      return !string_to_int_mapper::is_tau_transition(pn.accessor().get_label(transition));
    }};
    shortest_paths_matrix::distance_type current_distance{is_labelled(from) ? 1 : 0};
    current_distance += ctl::cast<shortest_paths_matrix::distance_type>(
        std::count_if(begin(transitions), end(transitions), is_labelled));

    shortest_paths.update_distance(from, to, current_distance);
    shortest_paths.update_path(from, to, transitions);
  }
}

/// Are all the nodes in the parallel section also contained in the path?
// CPL-6495: This may not always work. Consider a parallel section inside a parallel section, e.g.
bool contains_section(const std::vector<petri_net_transition_id>& path_i_j, size_t parallel_section_size,
                      size_t section_start, petri_net_transition_id join) {
  // Stop if reach end or if reach join node
  auto i{section_start};
  while (i < path_i_j.size() && path_i_j[i].id != join.id) {
    ++i;
  }

  const size_t node_count{i - section_start};
  return !(i != path_i_j.size() && node_count < parallel_section_size);
}

bool is_valid_path(const shortest_paths_matrix& shortest_paths, petri_net_accessor_with_parallel_sections& pn,
                   const std::vector<petri_net_transition_id>& path_i_j) {
  for (size_t i{0}; i < path_i_j.size(); ++i) {
    const auto& current_node{path_i_j[i]};

    if (pn.accessor().is_fork_node(current_node)) {
      const auto fork{current_node};
      const auto join{pn.par_sections().at_join(current_node)};
      const auto parallel_section_size{shortest_paths.get_distance(fork, join)};

      if (!contains_section(path_i_j, parallel_section_size, i, join)) {
        return false;
      }
    }
  }
  return true;
}

void update_path_from_to_via(shortest_paths_matrix& shortest_paths, petri_net_accessor_with_parallel_sections& pn,
                             const petri_net_transition& from, const petri_net_transition& to,
                             const petri_net_transition& middle) {
  const auto transition_from{from.transition};
  const auto transition_to{to.transition};
  const auto transition_middle{middle.transition};

  const auto dist_from_to{shortest_paths.get_distance(transition_from, transition_to)};
  const auto dist_from_middle{shortest_paths.get_distance(transition_from, transition_middle)};
  const auto dist_middle_to{shortest_paths.get_distance(transition_middle, transition_to)};

  if (dist_from_middle == shortest_paths_matrix::UNCONNECTED || dist_middle_to == shortest_paths_matrix::UNCONNECTED) {
    return;
  }
  if (pn.par_sections().is_section(transition_from, transition_to)) {
    return;
  }

  const auto path_from_middle{shortest_paths.get_path(transition_from, transition_middle)};
  const auto path_middle_to{shortest_paths.get_path(transition_middle, transition_to)};

  if (dist_from_to == shortest_paths_matrix::UNCONNECTED || dist_from_to > dist_from_middle + dist_middle_to) {
    std::vector<petri_net_transition_id> path_from_to{};
    path_from_to.push_back(transition_from);
    path_from_to.insert(path_from_to.end(), path_from_middle.begin(), path_from_middle.end());
    path_from_to.insert(path_from_to.end(), path_middle_to.begin(), path_middle_to.end());

    if (is_valid_path(shortest_paths, pn, path_from_to)) {
      // Remove transition_i from path
      // (erase() is ok because rebuilding it would require a relocation anyways)
      // if this proves to be too expensive one can rewrite "is_valid_path"
      // to accept transition_i separately, avoiding to insert it into path
      path_from_to.erase(path_from_to.begin());
      shortest_paths.update_distance(transition_from, transition_to, dist_from_middle + dist_middle_to);
      shortest_paths.update_path(transition_from, transition_to, std::move(path_from_to));
    }
  }
}

/**
 * Run algorithm similar to Floyd-Warshall, but with following modifications specific for Petri Nets:
 *  1- The distance between fork and its corresponding join nodes that form a parallel section
 *      is the number of transitions contained within the parallel section
 *  2- Keep track of shortest path between two transitions.
 *      When updating, not only the new distance must be smaller,
 *      but if the new path contains a fork and its corresponding join nodes,
 *      then it must contain all transitions present in the corresponding parallel section
 */
void main_loop_floyd(shortest_paths_matrix& shortest_paths, petri_net_accessor_with_parallel_sections& pn) {
  // Main loops. Do not change the order of the loops! (see Wikipedia entry)
  for (const auto& pn_transition_k : pn.accessor().get_transitions()) {
    for (const auto& pn_transition_i : pn.accessor().get_transitions()) {
      for (const auto& pn_transition_j : pn.accessor().get_transitions()) {
        update_path_from_to_via(shortest_paths, pn, pn_transition_i, pn_transition_j, pn_transition_k);
      }
    }
  }
}

void fix_self_path_fork_node(shortest_paths_matrix& shortest_paths, petri_net_accessor_with_parallel_sections& pn,
                             const petri_net_transition& pn_transition) {
  const auto fork{pn_transition.transition};
  const auto join{pn.par_sections().at_join(pn_transition.transition)};
  const auto distance_fork_join{shortest_paths.get_distance(fork, join)};
  const auto distance_join_fork{shortest_paths.get_distance(join, fork)};

  if (distance_join_fork != shortest_paths_matrix::UNCONNECTED) {
    const auto path_fork_join{shortest_paths.get_path(fork, join)};
    const auto path_join_fork{shortest_paths.get_path(join, fork)};

    std::vector<petri_net_transition_id> self_path{};
    self_path.reserve(path_fork_join.size() + path_join_fork.size());
    self_path.insert(self_path.end(), path_fork_join.begin(), path_fork_join.end());
    self_path.insert(self_path.end(), path_join_fork.begin(), path_join_fork.end());

    shortest_paths.update_distance(fork, fork, distance_join_fork + distance_fork_join);
    shortest_paths.update_path(fork, fork, std::move(self_path));
  } else {
    shortest_paths.update_distance(fork, fork, shortest_paths_matrix::UNCONNECTED);
    shortest_paths.update_path(fork, fork, {});
  }
}

void fix_self_path_not_fork_node(shortest_paths_matrix& shortest_paths, petri_net_accessor_with_parallel_sections& pn,
                                 const petri_net_transition& pn_transition) {
  const auto transition{pn_transition.transition};
  int32_t min_distance{std::numeric_limits<shortest_paths_matrix::distance_type>::max()};
  petri_net_transition_id closest_successor{0};

  for (const auto& place : pn_transition.out_places) {
    for (const auto& successor : pn.accessor()[place].out_transitions) {
      const auto distance_successor_i{shortest_paths.get_distance(successor, transition)};
      if (distance_successor_i != shortest_paths_matrix::UNCONNECTED && distance_successor_i < min_distance) {
        min_distance = distance_successor_i;
        closest_successor = successor;
      }
    }
  }

  if (closest_successor.id != 0) {
    const auto path_closest_successor_i{shortest_paths.get_path(closest_successor, transition)};

    std::vector<petri_net_transition_id> self_path{closest_successor};
    self_path.reserve(path_closest_successor_i.size() + 1);
    self_path.insert(self_path.end(), path_closest_successor_i.begin(), path_closest_successor_i.end());

    shortest_paths.update_path(transition, transition, path_closest_successor_i);
    if (!string_to_int_mapper::is_tau_transition(pn_transition.label)) {
      min_distance += 1;
    }
    shortest_paths.update_distance(transition, transition, min_distance);
  } else {
    shortest_paths.update_distance(transition, transition, shortest_paths_matrix::UNCONNECTED);
    shortest_paths.update_path(transition, transition, {});
  }
}

void fix_self_paths(shortest_paths_matrix& shortest_paths, petri_net_accessor_with_parallel_sections& pn) {
  for (const auto& pn_transition : pn.accessor().get_transitions()) {
    if (pn.accessor().is_fork_node(pn_transition.transition)) {
      fix_self_path_fork_node(shortest_paths, pn, pn_transition);
    } else {
      fix_self_path_not_fork_node(shortest_paths, pn, pn_transition);
    }
  }
}

}  // namespace

// Dijkstra from all nodes is faster because petri nets are sparse (unfoldings are trees)
//  There are also other algorithms for fast shortest-path computation for all pairs of nodes
shortest_paths_matrix find_shortest_paths(petri_net_accessor_with_parallel_sections& pn,
                                          const common::execution_context& context) {
  shortest_paths_matrix shortest_paths(pn.accessor().get_max_transition_id());

  set_initial_distances(shortest_paths, pn.accessor());
  set_parallel_section_distances(shortest_paths, pn, context);
  main_loop_floyd(shortest_paths, pn);
  fix_self_paths(shortest_paths, pn);

  return shortest_paths;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
