#include "../base_case_strategy.h"

namespace celonis::accelerator::operators::process {

bool empty_traces_base_case::is_applicable(const directly_follows_graph& dfg) {
  return dfg[boost::graph_bundle].log.contains_empty_trace > 0;
}

process_tree empty_traces_base_case::apply(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                           const common::execution_context& context,
                                           inductive_miner_statistics& miner_statistics,
                                           const cube::execution::tracking::stop_token& stop_token) {
  const auto counts{update_dfg(dfg)};
  return process_tree{{process_tree::exclusive{
      {std::vector{{process_tree::tau{counts.empty_count}},
                   inductive_miner_recurse(miner_config, dfg, context, miner_statistics, stop_token)}},
      {counts.empty_count, counts.nonempty_count}}}};
}

empty_traces_base_case::counts empty_traces_base_case::update_dfg(directly_follows_graph& dfg) {
  counts result{};
  auto& log{dfg[boost::graph_bundle].log};
  // Prepare the directly-follows graph:
  result.empty_count = log.contains_empty_trace;
  log.trace_count -= result.empty_count;
  result.nonempty_count = log.trace_count;
  log.contains_empty_trace = 0;
  return result;
}

}  // namespace celonis::accelerator::operators::process
