#include "../base_case_strategy.h"

namespace celonis::accelerator::operators::process {

bool single_activity_base_case::is_applicable(const directly_follows_graph& dfg) {
  return boost::num_vertices(dfg) == 1;
}

process_tree single_activity_base_case::apply(const directly_follows_graph& dfg) {
  const auto& log{dfg[boost::graph_bundle].log};
  process_tree::count_type total_count{dfg[0].count};  // how often activity executed in total
  process_tree single_activity{process_tree::activity{dfg[0].activity_id, total_count}};
  process_tree::count_type repetitions{total_count - log.trace_count};  // how many activity repetitions in total
  if (repetitions > 0) {
    process_tree::redo result{};
    result.children.emplace_back(std::move(single_activity));
    result.children.emplace_back(process_tree{process_tree::tau{repetitions}});
    result.child_redo_counts.emplace_back(total_count);
    result.child_redo_counts.emplace_back(repetitions);
    result.object_count = log.trace_count;
    return {result};
  }
  return single_activity;
}

}  // namespace celonis::accelerator::operators::process
