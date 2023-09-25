#pragma once

#include <unordered_map>
#include <vector>

#include "modules/operators/process/alignment/rl_align_configs.h"
#include "modules/operators/process/alignment/rl_statistics/statistics_base.h"

namespace celonis::accelerator::operators::process::alignment::rl_statistics {

class top_constraints final : public statistics_base {
  using alignment_cost_type = uint64_t;

  std::unordered_map<constraints_config, alignment_cost_type> current_run_;
  std::unordered_map<constraints_config, size_t> statistics_;

 public:
  // base interface
  void update_current_run(constraints_config constraints, alignment_cost_type cost) override {
    current_run_.insert_or_assign(constraints, cost);
  }
  void finish_current_run() override;

  // additional functions
  /**
   * Extract the best n parameter tuples
   * @param n the number of parameter tuples to retain
   * @return The best n parameter tuples given the statistics gathered so far
   */
  constraints_config_set get_top(size_t n) const;

  constraints_config_set get_all_best() const;

  /**
   * Merge other statistics into this
   * @param other The other statistics to merge into this one
   */
  void update(const top_constraints& other);
};

}  // namespace celonis::accelerator::operators::process::alignment::rl_statistics
