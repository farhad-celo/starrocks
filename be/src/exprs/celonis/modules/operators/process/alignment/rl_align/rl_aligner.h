#pragma once

#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/alignment/petri_net_information.h"
#include "modules/operators/process/alignment/rl_align/gap_filler.h"
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"
#include "modules/operators/process/alignment/rl_align/rl_statistics/optimal_alignment_set_cover.h"
#include "modules/operators/process/alignment/rl_align/rl_statistics/top_constraints.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

class rl_aligner {
 public:
  rl_aligner(const petri_net::safe_petri_net_data& pn_data_tt, const petri_net::safe_petri_net_data& pn_data_tf,
             const petri_net_information& pn_info, row_id num_analysis_run, row_id num_constraints_kept,
             rl_align_config cfg, const common::execution_context& ctx);

  // Returns the alignment (if any) and the number of optimizations solved
  [[nodiscard]] std::pair<trace_alignment_t, size_t> operator()(std::span<const row_id> variant, int max_cost,
                                                                const common::execution_context& context);

  // Getters
  [[nodiscard]] const rl_statistics::top_constraints& statistics_aggregator() const;

 private:
  void collect_statistics(const std::vector<size_t>& alignment_scores);
  // Based on the statistics collected so far, select the best constraints (if we are at that point)
  void select_best_constraints_if_enough_data();

  // Number of compute function invocations for this solver
  row_id alignments_so_far_{0};

  // Statistics for optimal computation
  rl_statistics::top_constraints statistics_aggregator_{};
  rl_statistics::optimal_alignment_set_cover set_cover_aggregator_{};

  petri_net::petri_net_accessor pn_tt_;
  petri_net::petri_net_accessor pn_tf_;
  const petri_net_information& pn_info_;
  row_id num_analysis_run_;
  row_id num_constraints_kept_;
  rl_align_config cfg_;

  gap_filler gap_filler_;
  constraints_config_set constraints_;
};

}  // namespace celonis::accelerator::operators::process::alignment::rl_align
