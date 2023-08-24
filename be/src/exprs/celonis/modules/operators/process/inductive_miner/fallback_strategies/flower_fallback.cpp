#include <vector>

#include "../fallback_strategy.h"

namespace celonis::accelerator::operators::process {

bool flower_fallback::is_applicable() { return true; }

process_tree flower_fallback::apply(const directly_follows_graph& dfg) {
  process_tree::redo result{};
  result.children.push_back({process_tree::tau{0}});
  result.object_count = dfg[boost::graph_bundle].log.trace_count;
  result.child_redo_counts.emplace_back(result.object_count);
  for (auto [it, last]{boost::vertices(dfg)}; it != last; ++it) {
    result.children.emplace_back(process_tree{process_tree::activity{dfg[*it].activity_id, dfg[*it].count}});
    const auto child_object_count{dfg[*it].count};
    result.child_redo_counts.emplace_back(child_object_count);
    result.child_redo_counts.front() += child_object_count;
  }

  // count of tau in loop is the sum of all activity occurrences + number of traces
  std::get<process_tree::tau>(result.children.front().node).object_count = result.child_redo_counts.front();

  return {result};
}

}  // namespace celonis::accelerator::operators::process
