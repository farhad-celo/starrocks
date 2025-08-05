#pragma once

#include <optional>
#include <vector>

#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/petri_net/transition_distances.h"
#include "modules/operators/process/alignment/sequence_aligner.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

/** This is basically a wrapper around the map, but we do it here to not pollute the consuming class */
template <typename SRC_TYPE, typename TGT_TYPE, typename STEP_TYPE>
struct path_cache {
  using tracked_path_t = memory::management::checked_vector_t<STEP_TYPE>;
  using maybe_tracked_path_t = std::optional<tracked_path_t>;

  using key_t = std::pair<SRC_TYPE, TGT_TYPE>;
  using cache_t =
      memory::management::checked_ska_hash_map_t<key_t, maybe_tracked_path_t, boost::hash<key_t>, std::equal_to<>>;

  path_cache(const common::execution_context& context, std::string_view allocation_message)
      : entries{memory::management::checked_allocator<cache_t>(context, LEGACY_EMBEDDED_ALLOC_MSG(allocation_message))} {}

  cache_t entries;
};

class gap_filler {
 public:
  using transition_type = petri_net::petri_net_transition_id;
  using marking_type = petri_net::marking_type;

  using transition_span_type = petri_net::petri_net_accessor::transition_span_type;
  using maybe_transition_span_type = std::optional<transition_span_type>;

  gap_filler(const petri_net::safe_petri_net_data& pn_data, int max_iterations, int max_insertions,
             petri_net::transition_distances_matrix transition_distances, const common::execution_context& context)
      : pn_accessor_{pn_data, context},
        paths_to_transition{context, legacy_embedded_ctl::PATHS_TO_TRANSITION_CACHE},
        paths_to_marking{context, legacy_embedded_ctl::PATHS_TO_MARKING_CACHE},
        max_iterations_{max_iterations},
        max_insertions_{max_insertions},
        transition_distances_{std::move(transition_distances)} {}

  trace_alignment_t complete_alignment(const petri_net::rl_problem_solution_t& solution, std::span<const row_id> trace,
                                       int max_cost, const common::execution_context& context);

 private:
  bool complete_to_sync_moves(const petri_net::rl_problem_solution_t& solution, trace_alignment& alignment,
                              petri_net::marking_type& current_marking, std::span<const row_id> trace, int max_cost,
                              const common::execution_context& context);

  bool complete_to_final_marking(trace_alignment& alignment, petri_net::marking_type& current_marking, int max_cost,
                                 const common::execution_context& context);

  maybe_transition_span_type get_enabling_transitions_to(const petri_net::marking_type& current_marking,
                                                         petri_net::petri_net_transition_id transition, int max_cost,
                                                         const common::execution_context& context);

  maybe_transition_span_type get_enabling_transitions_to(const petri_net::marking_type& current_marking,
                                                         const petri_net::marking_type& final_marking, int max_cost,
                                                         const common::execution_context& context);

  using paths_to_transition_cache_type = path_cache<marking_type, transition_type, transition_type>;
  using paths_to_marking_cache_type = path_cache<marking_type, marking_type, transition_type>;

  petri_net::petri_net_accessor pn_accessor_;
  paths_to_transition_cache_type paths_to_transition;
  paths_to_marking_cache_type paths_to_marking;

  // The maximum number of iterations to run a single completion search (between two moves or to the final marking)
  int max_iterations_;
  // The maximum number of insertions for a single completion search (between two moves or to the final marking)
  int max_insertions_;

  petri_net::transition_distances_matrix transition_distances_;
};

}  // namespace celonis::accelerator::operators::process::alignment::rl_align
