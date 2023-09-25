#pragma once

#include "modules/operators/process/alignment/petri_net/compute_behavioral_relations.h"
#include "modules/operators/process/alignment/petri_net/compute_shortest_path.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"

namespace celonis::accelerator::operators::process::alignment {

// TODO (goulart.e) it is bad that this holds information for both unfoldings reconnections
struct petri_net_information {
  petri_net_information(const petri_net::safe_petri_net_data& pn_data_tt,
                        const petri_net::safe_petri_net_data& pn_data_tf, const common::execution_context& context) {
    petri_net::petri_net_accessor_with_parallel_sections pn_accessor_tt{pn_data_tt};
    petri_net::petri_net_accessor_with_parallel_sections pn_accessor_tf{pn_data_tf};

    shortest_paths_tt = petri_net::find_shortest_paths(pn_accessor_tt, context);
    shortest_paths_tf = petri_net::find_shortest_paths(pn_accessor_tf, context);

    behavioral_relations_tt = petri_net::compute_behavioral_relations(pn_accessor_tt, shortest_paths_tt);
    behavioral_relations_tf = petri_net::compute_behavioral_relations(pn_accessor_tf, shortest_paths_tf);
  }

  petri_net::shortest_paths_matrix shortest_paths_tt{};
  petri_net::shortest_paths_matrix shortest_paths_tf{};

  petri_net::behavioral_relations_matrix behavioral_relations_tt{};
  petri_net::behavioral_relations_matrix behavioral_relations_tf{};
};

}  // namespace celonis::accelerator::operators::process::alignment
