#pragma once

#include "modules/operators/process/petri_net/behavioral_relations.h"
#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/transition_distances.h"

namespace celonis::accelerator::operators::process::alignment {

// TODO (goulart.e) it is bad that this holds information for both unfoldings reconnections
struct petri_net_information {
  petri_net_information(const petri_net::safe_petri_net_data& pn_data_tt,
                        const petri_net::safe_petri_net_data& pn_data_tf, const common::execution_context& context) {
    const petri_net::petri_net_accessor_with_parallel_sections pn_accessor_tt{pn_data_tt, context};
    const petri_net::petri_net_accessor_with_parallel_sections pn_accessor_tf{pn_data_tf, context};

    transition_distances_tt = petri_net::find_lower_bound_transition_distances(pn_accessor_tt, context);
    transition_distances_tf = petri_net::find_lower_bound_transition_distances(pn_accessor_tf, context);

    behavioral_relations_tt = petri_net::compute_behavioral_relations(pn_accessor_tt, transition_distances_tt);
    behavioral_relations_tf = petri_net::compute_behavioral_relations(pn_accessor_tf, transition_distances_tf);
  }

  petri_net::transition_distances_matrix transition_distances_tt{};
  petri_net::transition_distances_matrix transition_distances_tf{};

  petri_net::behavioral_relations_matrix behavioral_relations_tt{};
  petri_net::behavioral_relations_matrix behavioral_relations_tf{};
};

}  // namespace celonis::accelerator::operators::process::alignment
