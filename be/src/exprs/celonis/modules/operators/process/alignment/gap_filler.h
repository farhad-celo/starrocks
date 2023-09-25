#pragma once

#include <vector>

#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/alignment/petri_net/compute_shortest_path.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/sequence_aligner.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

class gap_filler {
 public:
  using transition_list_type = petri_net::transitions_container_t<petri_net::petri_net_transition_id>;

  gap_filler(const petri_net::safe_petri_net_data& pn_data, uint64_t max_depth,
             petri_net::shortest_paths_matrix source_distances, std::optional<sequence_aligner> fallback_aligner)
      : pn_accessor_{pn_data},
        max_depth_{max_depth},
        source_distances_{std::move(source_distances)},
        baseline_{std::move(fallback_aligner)} {}

  trace_alignment complete_alignment(const petri_net::rl_problem_solution_t& solution, std::span<const row_id> trace,
                                     const common::execution_context& context);

 private:
  void complete_to_sync_moves(const petri_net::rl_problem_solution_t& solution, trace_alignment& alignment,
                              petri_net::marking_type& current_marking, std::span<const row_id> trace,
                              uint64_t fallback_cost, const common::execution_context& context);

  bool complete_to_final_marking(trace_alignment& alignment, petri_net::marking_type& current_marking,
                                 uint64_t fallback_cost, const common::execution_context& context);

  transition_list_type get_enabling_transitions_to(const trace_alignment& alignment,
                                                   const petri_net::marking_type& current_marking, uint64_t max_cost,
                                                   petri_net::petri_net_transition_id transition,
                                                   const common::execution_context& context);

  transition_list_type get_enabling_transitions_to(const trace_alignment& alignment,
                                                   const petri_net::marking_type& current_marking, uint64_t max_cost,
                                                   const petri_net::marking_type& final_marking,
                                                   const common::execution_context& context);

  static constexpr int MAX_ITERATIONS{5'000};
  petri_net::petri_net_accessor pn_accessor_;
  petri_net::petri_net_path_cache path_cache_;
  uint64_t max_depth_;

  petri_net::shortest_paths_matrix source_distances_;
  std::optional<sequence_aligner> baseline_;
};

}  // namespace celonis::accelerator::operators::process::alignment
