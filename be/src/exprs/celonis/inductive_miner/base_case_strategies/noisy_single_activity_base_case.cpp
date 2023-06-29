#include "../base_case_strategy.h"
#include "ctl/conversion.h"

namespace celonis::accelerator::operators::process {

bool noisy_single_activity_base_case::is_applicable(const directly_follows_graph& dfg,
                                                    const dfg_filter_config& filter_config) {
  // Only try this base case, if vertices should be filtered.
  if (!filter_config.vertices) {
    return false;
  }

  // Assuming #occurences ~ Geo(p), compute the expected number of occurences of the
  // most likely parameter `p`.
  // Remember: there are no empty traces left due to `empty_traces_base_case`.
  if (boost::num_vertices(dfg) == 1) {
    const auto& log{dfg[boost::graph_bundle].log};
    const cel_float_t expected =
        static_cast<cel_float_t>(dfg[boost::vertex_bundle].count) / static_cast<cel_float_t>(log.trace_count);

    return expected < filter_config.vertices_max_avg_occurences;
  }
  return false;
}

process_tree noisy_single_activity_base_case::apply(const directly_follows_graph& dfg) {
  return process_tree{
      process_tree::activity{ctl::cast<process_tree::activity::activity_id_type>(dfg[boost::vertex_bundle].activity_id),
                             dfg[boost::vertex_bundle].count}};
}

}  // namespace celonis::accelerator::operators::process
