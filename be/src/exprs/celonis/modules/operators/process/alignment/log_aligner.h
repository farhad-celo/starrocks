#pragma once

#include <optional>
#include <unordered_map>

#include "modules/common/shared_types.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/operators/process/alignment/alignment_statistics.h"
#include "modules/operators/process/alignment/fallback/fallback_aligner.h"
#ifndef CELOSTAR
#include "modules/operators/process/alignment/log_alignment_result.h"
#endif
#include "modules/operators/process/alignment/petri_net_information.h"
#include "modules/operators/process/alignment/rl_align/gap_filler.h"
#include "modules/operators/process/alignment/rl_align/rl_aligner.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

struct log_aligner_config {
  [[nodiscard]] static log_aligner_config make_default();

  // Fitting prefix check
  int num_iterations_fitting_prefix{};
  int max_number_trailing_model_moves{};
  // Fallback aligner
  int num_iterations_shortest_trace{};
  // A-star
  int num_a_star_iterations{};
  // Relaxation labeling
  row_id num_analysis_runs{};
  size_t num_constraints_kept{};
  rl_align::rl_align_config rl_align_cfg{};

 private:
  // Can't default-construct it. Use the factory instead
  log_aligner_config() = default;
};

class log_aligner {
 public:
  using dict_mapping_type = std::optional<std::span<const row_id>>;

  explicit log_aligner(petri_net::safe_petri_net_data pn_data_tt, petri_net::safe_petri_net_data pn_data_tf,
                       dict_mapping_type pruned_dict_mapping, log_aligner_config config,
                       const common::execution_context& context)
      : pruned_dict_mapping_{pruned_dict_mapping},
        pn_data_tt_{std::move(pn_data_tt)},
        pn_data_tf_{std::move(pn_data_tf)},
        pn_information_{pn_data_tt_, pn_data_tf_, context},
        config_{std::move(config)} {}

  struct alignment_counters {
    size_t pruned_variants_computed_optimal{};
    size_t pruned_variants_computed_relaxation_labeling{};
    size_t optimizations_solved{};
    size_t successful_relaxation_labelings{};
    size_t alignment_cost{};
    size_t successfully_computed_alignments{};
    size_t time_optimal{};
    size_t time_relaxation_labeling{};

    alignment_counters& operator+=(const alignment_counters& rhs) noexcept;
    alignment_counters operator+(const alignment_counters& rhs) const noexcept;
  };

  struct agg_alignment_counters : alignment_counters {
    rl_align::constraints_config_set best_compatibilities{};
  };

  std::pair<vector_of_alignments, agg_alignment_counters> execute(
      common::execution_context& context, const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
      std::string_view operator_name);

  std::pair<trace_alignment_t, alignment_counters> align_variant(std::span<const row_id> pruned_mapped_trace,
                                                                 const fallback::fallback_aligner& fallback,
                                                                 rl_align::rl_aligner& rl_aligner,
                                                                 const common::execution_context& context) const;

 private:
  friend class log_aligner_wrapper;
  dict_mapping_type pruned_dict_mapping_;
  const petri_net::safe_petri_net_data pn_data_tt_;
  const petri_net::safe_petri_net_data pn_data_tf_;
  petri_net_information pn_information_;
  log_aligner_config config_;
};

using transition_id_to_str_mapping_t =
    std::unordered_map<petri_net::petri_net_transition_id, std::string, petri_net::hash_transition>;

class log_aligner_wrapper {
 public:
  [[nodiscard]] static log_aligner_wrapper create(const petri_net::petri_net_representation& pn_repr,
                                                  const std::unordered_set<std::string>& keep_transitions,
                                                  log_aligner::dict_mapping_type pruned_dict_mapping,
                                                  const log_aligner_config& log_aligner_cfg,
                                                  common::execution_context& operator_context,
                                                  const std::string& user_facing_name);

  const petri_net_information& precomputation_result() const { return aligner_.pn_information_; }

  std::tuple<vector_of_alignments, transition_id_to_str_mapping_t> operator()(
      const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
      common::execution_context& operator_context, const std::string& user_facing_name);

  const alignment_statistics& statistics() const { return stats_; }

 private:
  log_aligner_wrapper(input_output_mapper io_mapper, alignment_statistics stats,
                      petri_net::unfolding_representation unf_repr, log_aligner aligner)
      : io_mapper_{std::move(io_mapper)},
        stats_{std::move(stats)},
        unf_repr_{std::move(unf_repr)},
        aligner_{std::move(aligner)} {}
  input_output_mapper io_mapper_;
  alignment_statistics stats_;
  petri_net::unfolding_representation unf_repr_{};
  log_aligner aligner_;
};

}  // namespace celonis::accelerator::operators::process::alignment
