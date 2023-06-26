#include <boost/graph/connected_components.hpp>
#include <boost/graph/copy.hpp>
#include <boost/graph/filtered_graph.hpp>

#include "../cut_strategy.h"
#include "ctl/conversion.h"

namespace celonis::accelerator::operators::process {

namespace {

std::vector<directly_follows_graph> get_sub_dfgs_from_old_dfg(const directly_follows_graph& old_dfg, const cut_t& cut) {
  auto sub_dfgs{sub_dfgs::build_sub_dfgs_from_components(old_dfg, cut, false)};

  // Set the DFG level properties for each sub DFG
  sub_dfgs::add_start_and_end_vertices_from_old_dfg(old_dfg, cut.second, sub_dfgs);
  // NB: Empty trace count 0 for xor cut
  sub_dfgs::accumulate_non_empty_traces_per_dfg(sub_dfgs.sub_dfgs);
  return sub_dfgs.sub_dfgs;
}

}  // namespace

cut_t max_xor_cut::find(const directly_follows_graph& dfg) {
  const auto num_vertices{boost::num_vertices(dfg)};
  graph groupings{num_vertices};

  const auto& edges{boost::edges(dfg)};
  for (auto edge{edges.first}; edge != edges.second; ++edge) {
    boost::add_edge(boost::source(*edge, dfg), boost::target(*edge, dfg), groupings);
  }

  std::vector<size_t> component_mapping(num_vertices);
  const auto component_count{boost::connected_components(groupings, component_mapping.data())};

  return cut_t(component_count, component_mapping);
}

max_xor_cut::apply_result max_xor_cut::apply(inductive_miner_config& miner_config,
                                             const directly_follows_graph& old_dfg, const cut_t& cut,
                                             common::execution_context& context) {
  auto sub_eventlog_context{context.create_sub_context("max_xor_cut: compute sub-eventlogs", {})};
  const auto activity_count{miner_config.eventlog.activity_domain_count()};

  // create mappings from activity id to dfg id.
  const auto activity_dfg_mapping{cut_strategy::to_activity_dfg_map(cut, old_dfg, activity_count)};
  auto sub_eventlogs{miner_config.eventlog.split(activity_dfg_mapping)};
  auto dfgs{get_sub_dfgs_from_old_dfg(old_dfg, cut)};

  return {sub_eventlogs, dfgs};
}

process_tree::exclusive max_xor_cut::from_dfgs(inductive_miner_config miner_config, apply_result logs_and_dfgs,
                                               common::execution_context& context,
                                               inductive_miner_statistics& miner_statistics) {
  process_tree::exclusive result{};
  auto& [logs, dfgs]{logs_and_dfgs};
  debug_assert(logs.size() == dfgs.size());
  debug_assert(!logs.empty());
  result.child_object_counts.resize(logs.size());
  std::ranges::transform(dfgs, begin(result.child_object_counts),
                         [](const auto& dfg) { return dfg[boost::graph_bundle].log.trace_count; });
  result.children.resize(logs.size());
  std::ranges::transform(logs, dfgs, begin(result.children), [&](auto& log, auto& dfg) {
    miner_config.eventlog = std::move(log);
    return inductive_miner_recurse(miner_config, dfg, context, miner_statistics);
  });
  return result;
}

}  // namespace celonis::accelerator::operators::process
