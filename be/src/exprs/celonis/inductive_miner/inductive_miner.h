#pragma once

#include <vector>

#include "ctl/dynamic_bitset.h"
#include "modules/common/case_aligned_range.h"
#include "modules/cube/filter_bitset.h"
#include "modules/memory/column.h"
#include "modules/operators/process/inductive_miner/directly_follows_graph.h"
#include "modules/operators/process/inductive_miner/inductive_miner_statistics.h"
#include "modules/operators/process/inductive_miner/process_tree.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_config.h"

namespace celonis::accelerator::operators::process {

struct inductive_miner_operator_config {
  splittable_eventlog_config_t splittable_eventlog_config;
  dfg_filter_config filter_config{};
};

/**
 * This struct stores all the information the inductive miner needs in each
 * step of its recursion. If you have a special use case, you can tweak the
 * config as needed. However, `inductive_miner_operator` is the easier
 * interface to the inductive miner and should be fine for 99% of all use
 * cases. It sets up this config struct on its own.
 */
struct inductive_miner_config {
  splittable_eventlog eventlog{};

  /** Size of chunks for parallelism */
  size_t grain_size{};

  /**
   * At the moment, the configuration is never set. It should be configured
   * using PQL. It controls whether and how infrequent behavior should be
   * handled.
   */
  dfg_filter_config filter_config;

  inductive_miner_config(splittable_eventlog eventlog, size_t grain_size, const dfg_filter_config& filter_config = {})
      : eventlog{std::move(eventlog)}, grain_size{grain_size}, filter_config{filter_config} {}

  [[nodiscard]] static inductive_miner_config from_op_config(const inductive_miner_operator_config& config,
                                                             const common::execution_context& context) {
    const auto grain_size{grain_size_from_splittable_eventlog_config(config.splittable_eventlog_config)};
    return {splittable_eventlog::extract(config.splittable_eventlog_config, context), grain_size, config.filter_config};
  }
};

/**
 * The recursive call to the inductive miner.
 *
 * The output is not in normal form (i.e., it is the raw, unreduced process tree)
 * @param miner_config configuration parameters
 * @param dfg the directly-follows graph
 * @param context execution context
 * @param miner_statistics output parameter for statistics from the inductive miner
 * @return the "raw" (i.e., unreduced) process tree from recursively running the inductive miner
 */
process_tree inductive_miner_recurse(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                     common::execution_context& context, inductive_miner_statistics& miner_statistics);

struct inductive_miner_result {
  process_tree tree;
  bool is_valid{true};
};

/**
 * Call inductive_miner_raw and reduce the output process tree to normal form
 * @param miner_config configuration parameters
 * @param dfg the directly-follows graph
 * @param context execution context
 * @param miner_statistics output parameter for statistics from the inductive miner
 * @return the reduced process tree from the inductive_miner_raw call
 */
inductive_miner_result inductive_miner(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                       common::execution_context& context, inductive_miner_statistics& miner_statistics,
                                       const cel_string_t* dict = nullptr);

}  // namespace celonis::accelerator::operators::process
