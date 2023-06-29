#include "../cut_strategy.h"

namespace celonis::accelerator::operators::process {

noisy_xor_cut::apply_result noisy_xor_cut::apply(inductive_miner_config& miner_config,
                                                 const directly_follows_graph& old_dfg, const cut_t& cut,
                                                 common::execution_context& context) {
  auto sub_eventlog_context{context.create_sub_context("max_xor_cut: compute sub-eventlogs", {})};
  const auto activity_count{miner_config.eventlog.activity_domain_count()};
  const auto component_count{cut.first};

  // create mappings from activity id to dfg id.
  const auto activity_dfg_mapping{cut_strategy::to_activity_dfg_map(cut, old_dfg, activity_count)};

  // Filter deviating traces according to cut
  miner_config.eventlog.remove_if_per_trace(
      [&activity_dfg_mapping, &context, component_count](const auto first, const auto last) {
#ifdef CELOSTAR
        thread_local std::vector<uint64_t> component_counts(component_count);
#else
        thread_local auto component_counts{memory::tracking::make_static_array_for_overwrite<uint64_t>(
            component_count, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG), context)};
        std::fill(component_counts.begin(), component_counts.end(), 0);
#endif

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

  auto sub_eventlogs{miner_config.eventlog.split(activity_dfg_mapping)};

  // Cannot execute cut on DFG because counts might have changed after event log filtering
  std::vector<directly_follows_graph> dfgs(component_count);
  std::ranges::transform(sub_eventlogs, begin(dfgs),
                         [&](const auto& log) { return dfg::initialize_dfg(log, context, miner_config.grain_size); });

  return {sub_eventlogs, dfgs};
}

}  // namespace celonis::accelerator::operators::process
