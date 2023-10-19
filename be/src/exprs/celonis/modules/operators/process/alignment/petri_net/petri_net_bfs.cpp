#include "petri_net_bfs.h"

#include <algorithm>
#include <deque>

#include "log/log.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

std::vector<petri_net_transition_id> petri_net_bfs::path_to_transition(
    const petri_net_accessor& pn_accessor, const marking_type& source_marking,
    const petri_net_transition_id target_transition) {
  std::deque<marking_type> to_expand{source_marking};
  std::deque<marking_type> next_round{};

  for (uint64_t current_depth{0}; current_depth != max_depth_; ++current_depth) {
    if (to_expand.empty()) {
      break;
    }

    for (const auto& marking_to_expand : to_expand) {
      const auto enabled_transitions{pn_accessor.get_enabled_transitions(marking_to_expand)};

      for (const auto& enabled_transition : enabled_transitions) {
        const auto next_marking{pn_accessor.fire(marking_to_expand, enabled_transition)};

        if (enabled_transition == target_transition) {
          return get_enabling_path(marking_to_expand);
        }

        if (!already_visited_marking(next_marking) && next_marking != source_marking) {
          next_round.push_back(next_marking);
          paths_.insert({next_marking, {marking_to_expand, enabled_transition}});
        }
      }
    }

    std::swap(to_expand, next_round);
    next_round.clear();
  }

  if (!to_expand.empty()) {
    log::jwarn(fmt::format("{}: Couldn't reach target transition.", pn_accessor.get_user_visible_operator_name()),
               {{"num_iterations", max_depth_}});
  }
  return {};
}

bool petri_net_bfs::already_visited_marking(const marking_type& marking) const {
  return paths_.find(marking) != paths_.end();
}

// Goes backwards from marking to start to get path (must reverse the order in the end)
std::vector<petri_net_transition_id> petri_net_bfs::get_enabling_path(const marking_type& marking) const {
  std::vector<petri_net_transition_id> ret{};
  auto current_marking{marking};

  while (true) {
    const auto enabling_path{paths_.find(current_marking)};
    // Termination condition
    if (enabling_path == paths_.end()) {
      break;
    }

    const auto& [previous_marking, enabling_transition]{enabling_path->second};
    current_marking = previous_marking;
    ret.emplace_back(enabling_transition);
  }

  std::reverse(ret.begin(), ret.end());
  return ret;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
