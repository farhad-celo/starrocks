#pragma once

#include <set>
#include <unordered_map>

#include "modules/operators/process/alignment/rl_align_configs.h"
#include "modules/operators/process/alignment/rl_statistics/statistics_base.h"

namespace celonis::accelerator::operators::process::alignment::rl_statistics {

class optimal_alignment_set_cover final : public statistics_base {
  using alignment_cost_type = uint64_t;

  size_t current_run_index_{};
  std::unordered_map<constraints_config, alignment_cost_type> current_run_{};
  std::unordered_map<constraints_config, std::set<size_t>> statistics_{};

 public:
  // base interface
  void update_current_run(constraints_config constraints, alignment_cost_type cost) override {
    current_run_.insert_or_assign(constraints, cost);
  }
  void finish_current_run() override;

  // additional functions
  /**
   * greedy algorithm to approximate an optimal set of constraints covering all optimal alignments so far
   */
  constraints_config_set get_approximate_cover() &&;

  /**
   * Merge other statistics into this
   * @param other The other statistics to merge into this one
   */
  void update(const optimal_alignment_set_cover& other);
};

}  // namespace celonis::accelerator::operators::process::alignment::rl_statistics
