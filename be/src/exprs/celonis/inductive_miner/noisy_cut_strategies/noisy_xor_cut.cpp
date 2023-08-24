#ifdef CELOSTAR
#include "../cut_strategy.h"
#else
#include "modules/operators/process/inductive_miner/cut_strategy.h"
#endif

namespace celonis::accelerator::operators::process {

noisy_xor_cut::apply_result noisy_xor_cut::apply(inductive_miner_config& miner_config,
                                                 const directly_follows_graph& old_dfg, const cut_t& cut,
                                                 const common::execution_context& context,
                                                 const cube::execution::tracking::stop_token& stop_token) {
  auto sub_eventlog_context{context.create_sub_context("max_xor_cut: compute sub-eventlogs", {})};
  const auto activity_count{miner_config.eventlog().activity_domain_count()};
  const auto component_count{cut.first};

  // create mappings from activity id to dfg id.
  const auto activity_dfg_mapping{cut_strategy::to_activity_dfg_map(cut, old_dfg, activity_count)};

  // Filter deviating traces according to cut
  stop_token.stop_execution_if_requested();
  miner_config.eventlog().remove_if_per_trace(
      [&activity_dfg_mapping, component_count](const auto first, const auto last) {
        // In theory, the component_count could be as large as the number of activities, but in practice, this will
        // almost certainly be a single digit number. That justifies not tracking this data structure.
        std::vector<uint64_t> component_counts(component_count);

        auto trace_start{first};
        // Count number of events per component in the current trace
        for (auto current_event{first}; current_event != last; current_event++) {
          component_counts[activity_dfg_mapping[current_event->activity_id()]]++;
        }

        // Extract component with most events
        const auto max_component_id{
            static_cast<size_t>(std::distance(component_counts.begin(), std::ranges::max_element(component_counts)))};

        // Iterate over trace and remove events from non-max components
        auto new_trace_end{
            std::remove_if(trace_start, last, [&activity_dfg_mapping, max_component_id](auto activity_case_pair) {
              return activity_dfg_mapping[activity_case_pair.activity_id()] != max_component_id;
            })};

        return new_trace_end;
      });

  stop_token.stop_execution_if_requested();
  auto sub_eventlogs{miner_config.eventlog().split(activity_dfg_mapping)};

  // Cannot execute cut on DFG because counts might have changed after event log filtering
  stop_token.stop_execution_if_requested();
  std::vector<directly_follows_graph> dfgs(component_count);
  std::ranges::transform(sub_eventlogs, begin(dfgs),
                         [&](const auto& log) { return dfg::initialize_dfg(log, context, miner_config.grain_size()); });

  return {sub_eventlogs, dfgs};
}

}  // namespace celonis::accelerator::operators::process
