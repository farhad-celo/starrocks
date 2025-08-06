#pragma once

#include <vector>

#include "modules/common/int_types.h"
#include "modules/operators/process/petri_net/behavioral_relation.h"
#include "modules/operators/process/petri_net/petri_net_fwd.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

struct petri_net_transition_id;
class transition_distances_matrix;

class behavioral_relations_matrix {
 public:
  behavioral_relations_matrix() = default;

  explicit behavioral_relations_matrix(size_t number_of_transitions);

  void set_relation(petri_net_transition_id transition_i, petri_net_transition_id transition_j,
                    behavioral_relation relation);

  [[nodiscard]] behavioral_relation get_relation(petri_net_transition_id transition_i,
                                                 petri_net_transition_id transition_j) const;

 private:
  std::vector<behavioral_relation> behavioral_relations_{};
  size_t number_of_transitions_{0};
};

behavioral_relations_matrix compute_behavioral_relations(const petri_net_accessor_with_parallel_sections& pn,
                                                         const transition_distances_matrix& transition_distances);

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
