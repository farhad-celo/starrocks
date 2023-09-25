#pragma once

#include <unordered_map>

#include "modules/common/shared_types.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/operators/process/alignment/alignment_statistics.h"
#include "modules/operators/process/alignment/gap_filler.h"
#include "modules/operators/process/alignment/log_alignment_result.h"
#include "modules/operators/process/alignment/petri_net_information.h"
#include "modules/operators/process/alignment/rl_statistics/statistics_base.h"
#include "modules/operators/process/alignment/rl_statistics/top_constraints.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

// Returns the Petri net run corresponding to the shortest trace in the Petri net
std::optional<sequence_aligner::sequence_type> get_shortest_pn_trace(const petri_net::safe_petri_net_data& pn_data,
                                                                     const petri_net_information& pn_information,
                                                                     std::string_view operator_name,
                                                                     const common::execution_context& context);

class log_aligner {
 public:
  using dict_mapping_type = std::optional<std::span<const row_id>>;

  explicit log_aligner(petri_net::safe_petri_net_data pn_data_tt, petri_net::safe_petri_net_data pn_data_tf,
                       dict_mapping_type pruned_dict_mapping, rl_align_config rl_align_cfg, int num_a_star_iterations,
                       const common::execution_context& context)
      : num_a_star_iterations_{num_a_star_iterations},
        pruned_dict_mapping_{pruned_dict_mapping},
        pn_data_tt_{std::move(pn_data_tt)},
        pn_data_tf_{std::move(pn_data_tf)},
        pn_information_{pn_data_tt_, pn_data_tf_, context},
        rl_align_cfg_{std::move(rl_align_cfg)} {}

  struct alignment_counters {
    size_t pruned_variants_computed_optimal{};
    size_t pruned_variants_computed_relaxation_labeling{};
    size_t optimizations_solved{};
    size_t alignment_cost{};
    size_t time_optimal{};
    size_t time_relaxation_labeling{};

    alignment_counters& operator+=(const alignment_counters& rhs) noexcept;

    alignment_counters operator+(const alignment_counters& rhs) const noexcept;
  };

  struct agg_alignment_counters : alignment_counters {
    constraints_config_set best_compatibilities{};
  };

  std::pair<vector_of_alignments, agg_alignment_counters> execute(
      common::execution_context& context, const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
      std::string_view operator_name);

  std::pair<trace_alignment, alignment_counters> align_variant(
      std::span<const row_id> pruned_mapped_trace, gap_filler& filler, const constraints_config_set& constraints,
      const std::vector<rl_statistics::statistics_base*>& statistics, const common::execution_context& context) const;

  std::tuple<trace_alignment, std::vector<size_t>, size_t> get_optimal_alignment_and_statistics(
      std::span<const row_id> pruned_mapped_trace, const petri_net::petri_net_accessor& pn_accessor_tt,
      const constraints_config_set& constraints, gap_filler& filler, const common::execution_context& context) const;

 private:
  static constexpr row_id NUM_ANALYSIS_RUNS{1000};
  static constexpr size_t NUM_CONSTRAINTS_KEPT{8};  // we solve 8 constraints in parallel, so use a multiple of 8
  int num_a_star_iterations_{};
  dict_mapping_type pruned_dict_mapping_;
  const petri_net::safe_petri_net_data pn_data_tt_;
  const petri_net::safe_petri_net_data pn_data_tf_;
  petri_net_information pn_information_;
  rl_align_config rl_align_cfg_;
};

using transition_id_to_str_mapping_t =
    std::unordered_map<petri_net::petri_net_transition_id, std::string, petri_net::hash_transition>;

// Single entry point for alignment computation. This is horrible code but hopefully this class will be reworked soon
std::tuple<vector_of_alignments, alignment_statistics, transition_id_to_str_mapping_t> compute_alignments(
    const petri_net::petri_net_representation& pn_repr, const std::unordered_set<std::string>& keep_transitions,
    const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
    log_aligner::dict_mapping_type pruned_dict_mapping, int num_a_star_iterations, rl_align_config rl_align_cfg,
    common::execution_context& operator_context, const std::string& user_facing_name);

}  // namespace celonis::accelerator::operators::process::alignment
