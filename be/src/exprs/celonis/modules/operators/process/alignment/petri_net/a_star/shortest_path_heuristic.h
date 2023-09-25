#pragma once

#include <numeric>
#include <optional>
#include <vector>

#include "ctl/algorithm.h"
#include "modules/operators/process/alignment/petri_net/a_star/petri_net_wrapper.h"
#include "modules/operators/process/alignment/petri_net/compute_shortest_path.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

class heuristic_to_transition {
 public:
  using transition_type = petri_net_wrapper::transition_type;
  using marking_type = petri_net_wrapper::marking_type;
  using cost_type = int32_t;

  heuristic_to_transition(petri_net_transition_id target, const shortest_paths_matrix& transition_metric,
                          petri_net_accessor& petri_net)
      : target_{target}, transition_metric_{transition_metric}, petri_net_{petri_net} {}

  [[nodiscard]] cost_type get_weight(transition_type transition) const;

  [[nodiscard]] std::optional<cost_type> estimate(const marking_type& marking) const;

  [[nodiscard]] bool is_target(const petri_net::marking_type& marking) const {
    return ctl::contains(petri_net_.get_enabled_transitions(marking), target_);
  }

 private:
  petri_net_transition_id target_;
  const shortest_paths_matrix& transition_metric_;
  petri_net_accessor& petri_net_;
};

class heuristic_to_marking {
 public:
  using transition_type = petri_net_wrapper::transition_type;
  using marking_type = petri_net_wrapper::marking_type;
  using cost_type = int32_t;

  heuristic_to_marking(petri_net::marking_type target, const shortest_paths_matrix& transition_distances,
                       petri_net_accessor& petri_net)
      : target_{std::move(target)},
        target_transitions_{petri_net.compatible_generating_transitions(target_)},
        transition_distances_{transition_distances},
        petri_net_{petri_net} {}

  [[nodiscard]] cost_type get_weight(transition_type transition) const;

  [[nodiscard]] std::optional<cost_type> estimate(const petri_net::marking_type& marking) const;

  [[nodiscard]] bool is_target(const petri_net::marking_type& marking) const { return marking == target_; }

 private:
  petri_net::marking_type target_;
  std::vector<petri_net_transition_id> target_transitions_;
  const shortest_paths_matrix& transition_distances_;
  petri_net_accessor& petri_net_;
};

class heuristic_to_markings {
 public:
  using transition_type = petri_net_wrapper::transition_type;
  using marking_type = petri_net_wrapper::marking_type;
  using cost_type = int32_t;

  heuristic_to_markings(std::vector<petri_net::marking_type> targets, const shortest_paths_matrix& transition_distances,
                        petri_net_accessor& petri_net);

  [[nodiscard]] cost_type get_weight(transition_type transition) const;

  [[nodiscard]] std::optional<cost_type> estimate(const petri_net::marking_type& marking) const;

  [[nodiscard]] bool is_target(const petri_net::marking_type& marking) const {
    return ctl::contains(target_markings_, marking);
  }

 private:
  std::vector<petri_net::marking_type> target_markings_;
  std::vector<petri_net_transition_id> target_transitions_;
  const shortest_paths_matrix& metric_;
  petri_net_accessor& petri_net_;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
