#pragma once

#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"

namespace celonis::accelerator::operators::process::alignment::rl_align::rl_statistics {

/**
 * Interface for gathering statistics related to relaxation-labeling parameters
 *
 * The relaxation labeling part of the alignment algorithm needs a tuple of parameters (weights).
 * A priori, those are unknown, so we do the alignment on a potentially large set of parameter tuples,
 * and pick only the best alignment.
 * Since this is costly, we want to gather information about the "best" parameter tuples, and after a few alignments,
 * try to limit the feasible set of parameter tuples.
 *
 * This interface is used in the algorithm to gather the information needed to restrict the feasible set.
 */
struct statistics_base {
  virtual ~statistics_base() = default;

  using alignment_cost_type = uint64_t;

  /**
   * This method is called for each parameter tuple that was used during relaxation labeling for a single alignment
   * @param constraints the parameter tuple
   * @param cost the cost of the alignment that was computed with the given parameter tuple
   */
  virtual void update_current_run(constraints_config constraints, alignment_cost_type cost) = 0;

  /**
   * This method is called after all parameter tuples and their respective alignment costs of a single trace/variant
   * have been recorded with @update_current_run
   */
  virtual void finish_current_run() = 0;
};

}  // namespace celonis::accelerator::operators::process::alignment::rl_align::rl_statistics
