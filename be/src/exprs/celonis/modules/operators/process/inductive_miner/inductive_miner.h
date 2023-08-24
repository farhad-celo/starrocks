#pragma once

#ifdef CELOSTAR
#include "exprs/celonis/variant.h"
#endif
#include "modules/cube/execution/tracking/stop_token.h"
#include "modules/operators/process/inductive_miner/directly_follows_graph_fwd.h"
#include "modules/operators/process/inductive_miner/inductive_miner_config.h"
#include "modules/operators/process/inductive_miner/inductive_miner_statistics.h"
#include "modules/operators/process/inductive_miner/process_tree.h"

namespace celonis::accelerator::operators::process {

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
                                     const common::execution_context& context,
                                     inductive_miner_statistics& miner_statistics,
                                     const cube::execution::tracking::stop_token& stop_token);

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
                                       inductive_miner_statistics& miner_statistics,
                                       const cube::execution::tracking::stop_token& stop_token,
                                       const cel_string_t* dict = nullptr);

}  // namespace celonis::accelerator::operators::process
