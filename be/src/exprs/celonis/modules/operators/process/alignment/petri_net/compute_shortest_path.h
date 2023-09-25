#pragma once

#include <vector>

#include "modules/common/int_types.h"
#include "modules/operators/process/alignment/petri_net/petri_net_bfs.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/petri_net/petri_net_fwd.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

class shortest_paths_matrix {
 public:
  using distance_type = int32_t;

  static constexpr distance_type UNCONNECTED{-1};

  shortest_paths_matrix() = default;

  explicit shortest_paths_matrix(size_t number_of_transitions);

  [[nodiscard]] distance_type get_distance(petri_net_transition_id transition_from,
                                           petri_net_transition_id transition_to) const;

  void update_distance(petri_net_transition_id transition_from, petri_net_transition_id transition_to,
                       distance_type new_distance);

  [[nodiscard]] const std::vector<petri_net_transition_id>& get_path(petri_net_transition_id from,
                                                                     petri_net_transition_id to) const;

  void update_path(petri_net_transition_id from, petri_net_transition_id to,
                   std::vector<petri_net_transition_id> new_path);

 private:
  std::vector<distance_type> distances_{};

  std::vector<std::vector<petri_net_transition_id>> paths_{};

  size_t number_of_transitions_{0};
};

/**
 * Floyd Algorithm for shortest path computation
 *  See: https://en.wikipedia.org/wiki/Floyd%E2%80%93Warshall_algorithm
 * Code is adapted for PetriNets
 *  because in PetriNets distance must consider pre-/post-conditions
 */
shortest_paths_matrix find_shortest_paths(petri_net_accessor_with_parallel_sections& pn,
                                          const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
