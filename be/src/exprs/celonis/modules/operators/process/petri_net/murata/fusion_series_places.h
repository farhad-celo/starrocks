#pragma once

#include <optional>
#include <unordered_set>

#include "modules/operators/process/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

// TODO (goulart.e) once (if) we have general Petri nets we can turn this into two rules:
//  Arcs have weight == 1 -> can do whatever
//  Arcs have weight != 1 -> can only apply if places do not have a common transition in their presets
class fusion_series_places {
 public:
  using node_id_type = petri_net::petri_net_builder::node_id_type;

  /**
   * DEFINED AS:
   *  - Node is a transition
   *  - The transition is not "keep"
   *  - Node is SESE with the same arc weights = 1
   *      Notice that the arcs could also have weights != 1, but then this would require that "redirect" arc does
   *      not exist (otherwise an unsound net could become sound after reduction). Since that we only consider safe
   *      Petri nets, then arcs anyways can't have weights > 1
   *  - Input and output places are distinct
   *  - Input place has only one outgoing transition
   *
   * Because we represent markings as bitsets, the following extra constraints are needed:
   *  - The "redirect arc" does not exist
   *  - The input and output places are not simultaneously initial/final places
   */
  static std::optional<fusion_series_places> try_build(const petri_net_builder& pn,
                                                       const std::unordered_set<node_id_type>& keep_transitions,
                                                       const node_id_type& node_id);

  /**
   * The transition and its incoming/outgoing arcs are removed
   * If the input place is in the initial/final marking, this is passed to the output place
   * All arcs leading to the input place are also added to the output place
   * The input place and its incoming/outgoing arcs are removed
   */
  void apply(petri_net::petri_net_builder& pn) const;

 private:
  fusion_series_places(node_id_type in_place, node_id_type transition, node_id_type out_place)
      : in_place_{std::move(in_place)}, transition_{std::move(transition)}, out_place_{std::move(out_place)} {}

  node_id_type in_place_{};
  node_id_type transition_{};
  node_id_type out_place_{};
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
