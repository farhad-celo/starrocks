#pragma once

#include <boost/graph/adjacency_list.hpp>

#ifdef CELOSTAR
#include "inductive_miner/directly_follows_graph.h"
#include "inductive_miner/inductive_miner.h"
#include "inductive_miner/process_tree.h"
#else
#include "modules/operators/process/inductive_miner/directly_follows_graph.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"
#include "modules/operators/process/inductive_miner/process_tree.h"
#endif

namespace celonis::accelerator::operators::process {

struct empty_log_base_case {
  /**
   * This base case checks whether there is no activity left in the log, i.e.
   * the log consists of empty traces only. If so, a silent activity is
   * returned.
   */
  static bool is_applicable(const directly_follows_graph& dfg);
  static process_tree apply();
};

class empty_traces_base_case {
  struct counts {
    process_tree::count_type empty_count{};
    process_tree::count_type nonempty_count{};
  };

 public:
  /**
   * This base case removes empty traces from the log. If there are few enough
   * to be considered noise, they are simply removed. If there are to many, an
   * exclusive-choice cut (with a silent activity as the only other branch) is
   * performed.
   *
   * It uses `dfg_filter_config::vertices_threshold`.
   * TODO: Something like `log_threshold` would be more appropriate. Consider to create a
   * `inductive_miner_filter_config`.
   */
  static bool is_applicable(const directly_follows_graph& dfg);
  static process_tree apply(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                            common::execution_context& context, inductive_miner_statistics& miner_statistics);
  static counts update_dfg(directly_follows_graph& dfg);
};

struct single_activity_base_case {
  /**
   * This base case checks whether there is only a single activity left. It
   * requires all empty traces to be removed from the log. The easiest way to
   * do so is to use `empty_traces_base_case`.
   */
  static bool is_applicable(const directly_follows_graph& dfg);
  static process_tree apply(const directly_follows_graph& dfg);
};

struct noisy_single_activity_base_case {
  /**
   * This base case checks whether there is only a single activity left and
   * whether this activity might be considered as a singleton.
   *
   * It uses `dfg_filter_config::vertices_max_avg_occurences`.
   */
  static bool is_applicable(const directly_follows_graph& dfg, const dfg_filter_config& filter_config);
  static process_tree apply(const directly_follows_graph& dfg);
};

}  // namespace celonis::accelerator::operators::process
