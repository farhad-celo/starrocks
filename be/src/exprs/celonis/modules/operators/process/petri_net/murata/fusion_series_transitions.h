#pragma once

#include "modules/operators/process/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

/// This file implements the fusion of series transitions rule, which has two particular cases to care
///  Both cases are distinguished as separated classes, although this wouldn't have been necessary

class fusion_series_transition_and_tau {
 public:
  using node_id_type = petri_net::petri_net_builder::node_id_type;

  /**
   * DEFINED AS:
   *  - The node is a place
   *  - The place is SESE with the same arc weights = 1
   *      Notice that the arcs could also have weights != 1, but then this would require that "redirect" arc does
   *      not exist (otherwise an unsound net could become sound after reduction). Since that we only consider safe
   *      Petri nets, then arcs anyways can't have weights > 1
   *  - The place's output transitions is not "keep"
   *  - The output transition has only one pre_condition (the place)
   *  - The input and output transitions are distinct
   *  - The place is not part of the final marking
   *
   * Because we represent markings as bitsets, the following extra constraints are needed:
   *  - The post-sets of the input and output transitions are disjoint
   *  - If the place is part of the initial marking, then the output places of its output transition are not
   */
  static std::optional<fusion_series_transition_and_tau> try_build(
      const petri_net_builder& pn, const std::unordered_set<node_id_type>& keep_transitions,
      const node_id_type& node_id);

  /**
   * If the place is in the initial marking, this is passed to the output places of its output transition
   * All outgoing arcs from the output transition are also added to the input transition
   * The place and its incoming/outgoing arcs are removed
   * The output transition and its incoming/outgoing arcs are removed
   */
  void apply(petri_net::petri_net_builder& pn) const;

 private:
  fusion_series_transition_and_tau(node_id_type in_transition, node_id_type place, node_id_type out_transition)
      : in_transition_{std::move(in_transition)},
        place_{std::move(place)},
        out_transition_{std::move(out_transition)} {}

  node_id_type in_transition_{};
  node_id_type place_{};
  node_id_type out_transition_{};
};

class fusion_series_tau_and_keep_transition {
 public:
  using node_id_type = petri_net::petri_net_builder::node_id_type;

  /**
   * DEFINED AS:
   *  - The node is a place
   *  - The place is SESE with arc weights = 1
   *  - The input and output transitions are distinct
   *  - The place's input transition is not "keep" and the node's output transition is "keep"
   *  - The input transition has only one output place (the place)
   *  - The place is not part of the initial marking
   *    (otherwise, it is easy to construct a net that would change its language after reduction)
   *
   * Because we represent markings as bitsets, the following extra constraint is needed:
   *  - The pre-sets of the input and output transitions are disjoint
   *  - If the place is part of the final marking, then the input places of its input transition are not
   */
  static std::optional<fusion_series_tau_and_keep_transition> try_build(
      const petri_net_builder& pn, const std::unordered_set<node_id_type>& keep_transitions,
      const node_id_type& node_id);

  /**
   * If the place is in the final marking, this is passed to the input places of its input transition
   * All arcs leading to the input transition are added to the output transition
   * The place and its incoming/outgoing arcs are removed
   * The input transition and its incoming/outgoing arcs are removed
   */
  void apply(petri_net::petri_net_builder& pn) const;

 private:
  fusion_series_tau_and_keep_transition(node_id_type in_transition, node_id_type place, node_id_type out_transition)
      : in_transition_{std::move(in_transition)},
        place_{std::move(place)},
        out_transition_{std::move(out_transition)} {}

  node_id_type in_transition_{};
  node_id_type place_{};
  node_id_type out_transition_{};
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
