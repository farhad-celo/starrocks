#pragma once

#include <optional>

#ifdef CELOSTAR
#include "inductive_miner/cut_strategy.h"
#include "inductive_miner/directly_follows_graph.h"
#include "inductive_miner/inductive_miner.h"
#include "inductive_miner/process_tree.h"
#else
#include "modules/operators/process/inductive_miner/cut_strategy.h"
#include "modules/operators/process/inductive_miner/directly_follows_graph.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"
#include "modules/operators/process/inductive_miner/process_tree.h"
#endif

namespace celonis::accelerator::operators::process {

struct activity_once_per_trace {
  /**
   * This fallback checks whether there is an activity that appears precisely once in each trace.
   * If so this activity is filtered out of the event log and put concurrent to the result of the inductive miner on the
   * remainder event log. [Leemans, S. J. J. (2017). Robust process mining with guarantees Eindhoven: Technische
   * Universiteit Eindhoven]
   */
  [[nodiscard]] static cut_t find(const inductive_miner_config& miner_config, const directly_follows_graph& dfg,
                                  const common::execution_context& context,
                                  const cube::execution::tracking::stop_token& stop_token);

  using apply_result = cut_strategy::apply_result;

  static apply_result apply(inductive_miner_config& miner_config, const directly_follows_graph& old_dfg,
                            const cut_t& cut, const common::execution_context& context,
                            const cube::execution::tracking::stop_token& stop_token);

  [[nodiscard]] static process_tree::parallel from_dfgs(const inductive_miner_config& miner_config, apply_result dfgs,
                                                        const common::execution_context& context,
                                                        inductive_miner_statistics& miner_statistics,
                                                        const cube::execution::tracking::stop_token& stop_token);
};

struct activity_concurrent {
  /**
   * This fallback checks whether there is an activity whose removal would yield a valid cut.
   * If so, this generates the tree +( x(< activity>,tau) or *(<activity>,tau) , <discovered_cut>).
   *
   * TODO (p.schumacher) CPL-7800 this requires a full pass over the log for each removed activity. We have to benchmark
   * the performance to better understand the impact of this fallback.
   */
  // does not follow the usual find and apply pattern as the find cut requires to apply the cut
  static std::optional<process_tree> apply_if_applicable(inductive_miner_config& miner_config,
                                                         const directly_follows_graph& dfg,
                                                         const common::execution_context& context,
                                                         inductive_miner_statistics& miner_statistics,
                                                         const cube::execution::tracking::stop_token& stop_token);

 protected:
  struct cut_and_dfgs {
    cut_t cut;
    std::vector<directly_follows_graph> dfgs;
  };
  static std::vector<cut_and_dfgs> compute_parallel_dfgs(const inductive_miner_config& miner_config,
                                                         const directly_follows_graph& dfg,
                                                         const common::execution_context& context,
                                                         const cube::execution::tracking::stop_token& stop_token);
};

using tau_loop_split_result = size_t;

struct tau_loop_fallback_base {
  static process_tree::redo from_dfgs(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                      tau_loop_split_result loop_count, const common::execution_context& context,
                                      inductive_miner_statistics& miner_statistics,
                                      const cube::execution::tracking::stop_token& stop_token) {
    const auto object_count{dfg[boost::graph_bundle].log.trace_count};
    auto redo_child{inductive_miner_recurse(miner_config, dfg, context, miner_statistics, stop_token)};
    return process_tree::redo{{{std::move(redo_child), {process_tree::tau{loop_count}}}},
                              object_count - loop_count,
                              {object_count, loop_count}};
  }
};

struct strict_tau_loop_fallback final : public tau_loop_fallback_base {
  explicit strict_tau_loop_fallback() = default;
  /**
   * This fallback checks whether there is an end activity directly followed
   * by an end activity within (!) a trace. If so, these traces are split and
   * the miner is rerun on the split event log.
   */
  static bool is_applicable(inductive_miner_config& miner_config, const directly_follows_graph& dfg,
                            const common::execution_context& context);

  static tau_loop_split_result apply(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                     const common::execution_context& context);
};

struct slack_tau_loop_fallback final : public tau_loop_fallback_base {
  explicit slack_tau_loop_fallback() = default;
  /**
   * This fallback checks whether there is a start activity in the middle (!)
   * of a trace. If so, these traces are split and the miner is rerun on the
   * split event log.
   */
  static bool is_applicable(const inductive_miner_config& miner_config, const directly_follows_graph& dfg,
                            const common::execution_context& context);

  static tau_loop_split_result apply(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                     const common::execution_context& context);
};

struct flower_fallback {
  /**
   * This fallback is always applicable. It should only be used as a last
   * resort, if we know absolutely nothing about the log. It creates the flower
   * model described by van der Aalst: REDO(tau, act_1, ..., act_n).
   */
  static bool is_applicable();

  static process_tree apply(const directly_follows_graph& dfg);
};

}  // namespace celonis::accelerator::operators::process
