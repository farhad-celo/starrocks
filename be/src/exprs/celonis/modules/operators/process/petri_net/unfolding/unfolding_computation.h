#pragma once

#include <queue>

#include <boost/functional/hash.hpp>

#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"
#include "modules/operators/process/petri_net/unfolding/total_adequate_order.h"
#include "unfolding_entities.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding {

using possible_extensions_queue_t =
    std::priority_queue<unfolding_event*, std::vector<unfolding_event*>, min_heap_total_adequate_order>;
using unfolding_condition_pair_t = std::pair<unfolding_condition*, unfolding_condition*>;
using co_occurrences_t = std::unordered_set<unfolding_condition_pair_t, boost::hash<unfolding_condition_pair_t>>;

/**
 * Implements unfolding_net computation as described at:
 *  Khomenko Victor: Model Checking Based on Prefixes of Petri Net Unfoldings (page 35)
 *
 * The chosen order is Total Adequate Order described at (REQUIRES NET TO BE 1-SAFE):
 *  Javier Esparza: An Improvement of McMillan's Unfolding Algorithm
 *
 * The computation of set of Possible Extensions follows the procedure described at:
 *  Khomenko Victor: Model Checking Based on Prefixes of Petri Net Unfoldings (page 38)
 *
 * update_co_occurrences (co-set update) is implemented as described at:
 *  Javier Esparza, Stefa Romer: An Unfolding Algorithm for Synchronous Products of Transition Systems
 *
 * For now, a simple std::priority_queue is used with total order as described in Esparza's paper
 *  this can lead to multiple unnecessary (and expensive) foata_normal_forms comparisons
 *  if this reveals to be a bottleneck, one can replace std::priority_queue with a more clever
 *  implementation as described in Esparza's paper (section 8: implementation)
 *
 * If still the unfolding_net computation step proves too expensive, one can try to improve it by using
 * preset trees (Section 4.3 of Victor Komenko's PhD thesis).
 */
class unfolding_computation {
 public:
  explicit unfolding_computation(const petri_net_accessor& pn_accessor) noexcept;

  unfolding_representation compute_unfolding();

 private:
  unfolding_net unfolding_{};
  std::unordered_set<unfolding_event*> cutoff_events_{};
  possible_extensions_queue_t possible_extensions_{};
  co_occurrences_t co_occurrences_{};
  const petri_net_accessor& pn_accessor_;

  void add_initial_conditions();

  void add_initial_possible_extensions();

  [[nodiscard]] unfolding_event* get_minimal_event() const;

  [[nodiscard]] bool is_cutoff(unfolding_event* event) const;

  void expand_unfolding(unfolding_event* event);

  /// See Unfolding Algorithm for Synchronous Products of Transition Systems (proposition 5)
  void update_co_occurrences(unfolding_event* event);

  void update_possible_extensions(unfolding_event* event);

  [[nodiscard]] std::unordered_set<petri_net_transition_id, hash_transition> get_possible_transitions_to_extend(
      unfolding_event* event) const;

  /// Using that cond, event concurrent <=> cond concurrent to all cond' in *event
  /// The proof is intuitive
  [[nodiscard]] std::vector<unfolding_condition*> concurrent_conditions_for_event(unfolding_event* event) const;

  [[nodiscard]] bool co_occurs(unfolding_condition* condition_1, unfolding_condition* condition_2) const;

  void cover(std::unordered_set<unfolding_event*>& extensions, const std::vector<unfolding_condition*>& C,
             const petri_net_transition& pn_transition, const std::unordered_set<unfolding_condition*>& preset);

  void extend_possible_extensions(const std::unordered_set<unfolding_event*>& extensions);

  [[nodiscard]] unfolding_representation get_unfolding_representation() const;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding
