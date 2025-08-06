#pragma once

#include <vector>

#include "modules/memory/management/memory_checked_containers.h"
#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

/** Instance of a BFS run on a PetriNet */
class petri_net_bfs {
 public:
  explicit petri_net_bfs(uint64_t max_depth, const common::execution_context& context)
      : max_depth_{max_depth},
        paths_{memory::management::checked_allocator<directed_edges_t>(context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))} {}

  [[nodiscard]] std::vector<petri_net_transition_id> path_to_transition(const petri_net_accessor& pn_accessor,
                                                                        const marking_type& source_marking,
                                                                        petri_net_transition_id target_transition);

 private:
  uint64_t max_depth_;
  using predecessor_and_transition_t = std::pair<marking_type, petri_net_transition_id>;
  using edge_t = std::pair<const marking_type, predecessor_and_transition_t>;
  using directed_edges_t = memory::management::checked_unordered_map<marking_type, predecessor_and_transition_t,
                                                                     boost::hash<marking_type>, std::equal_to<>>;
  directed_edges_t paths_;

  [[nodiscard]] bool already_visited_marking(const marking_type& marking) const;

  [[nodiscard]] std::vector<petri_net_transition_id> get_enabling_path(const marking_type& marking) const;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
