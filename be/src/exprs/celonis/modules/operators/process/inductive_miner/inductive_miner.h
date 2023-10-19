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
 * Used to log a warning with some additional information on the inductive miner parameters should the inductive miner
 * fail/timeout
 */
void log_inductive_miner_statistics_on_fail(std::string_view operator_name,
                                            const inductive_miner_statistics& miner_statistics,
                                            const inductive_miner_config& operator_config,
                                            const directly_follows_graph& dfg);

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
 * Call the inductive miner with additional logging in case of failure or timeout.
 * @param miner_config configuration parameters
 * @param dfg the directly-follows graph
 * @param context execution context
 * @param miner_statistics output parameter for statistics from the inductive miner
 * @return the reduced process tree from the inductive_miner_raw call
 */
inductive_miner_result invoke_verbose_inductive_miner(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                                      inductive_miner_statistics& miner_statistics,
                                                      const cube::execution::tracking::stop_token& stop_token,
                                                      std::string_view operator_name,
                                                      const cel_string_t* dict = nullptr);

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
