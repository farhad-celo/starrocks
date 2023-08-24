#include <numeric>
#include <vector>

#include <boost/dynamic_bitset.hpp>

#include "../fallback_strategy.h"
#include "ctl/conversion.h"
#include "modules/common/for_each_group.h"
#ifndef CELOSTAR
#include "modules/cube/filter_bitset.h"
#include "modules/memory/column.h"
#endif

namespace celonis::accelerator::operators::process {

namespace {

size_t split_dfg_slack_tau_style(directly_follows_graph& dfg) {
  size_t result{};
  for (auto& [start_vertex, start_count] : dfg[boost::graph_bundle].start_vertices) {
    size_t added_starts_count{};
    for (auto [edge_it, last_edge]{boost::in_edges(start_vertex, dfg)}; edge_it != last_edge; ++edge_it) {
      added_starts_count += dfg[*edge_it].count;
      dfg[boost::graph_bundle].end_vertices[boost::source(*edge_it, dfg)] += dfg[*edge_it].count;
    }
    result += added_starts_count;
    start_count += added_starts_count;
    boost::clear_in_edges(start_vertex, dfg);
  }
  dfg[boost::graph_bundle].log.trace_count += result;
  return result;
}

size_t split_log_and_dfg(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                         const common::execution_context& context) {
  // compute the number of additional sub-traces and already split the dfg
  const auto num_additional_traces{split_dfg_slack_tau_style(dfg)};
#ifdef CELOSTAR
  // In Celostar, we use a fixed 32bit space.
#else
  // ensure that our event-log has enough "space" to accommodate additional group ids
  miner_config.eventlog() = splittable_eventlog::canonicalize_if_necessary(std::move(miner_config.eventlog()),
                                                                           num_additional_traces, context);
#endif

  std::visit(
      [&miner_config, &dfg](auto view) {
        using activity_id_type = typename std::decay_t<decltype(view.front())>::first_type;
        using group_id_type = typename std::decay_t<decltype(view.front())>::second_type;
        std::set<activity_id_type> start_activities{};
        std::ranges::transform(dfg[boost::graph_bundle].start_vertices,
                               std::inserter(start_activities, end(start_activities)),
                               [&dfg](auto p) { return dfg[p.first].activity_id; });
        std::map<activity_id_type, size_t> end_activities_to_vertices{};
        std::ranges::transform(dfg[boost::graph_bundle].end_vertices,
                               std::inserter(end_activities_to_vertices, end(end_activities_to_vertices)), [&](auto p) {
                                 return std::pair{dfg[p.first].activity_id, p.first};
                               });
        const auto is_start_activity{
            [&start_activities](auto a) { return start_activities.contains(a.activity_id_raw()); }};
        const auto find_next_start_activity{[&](auto first, auto last) {
          return first == last ? first : std::ranges::find_if(std::next(first), last, is_start_activity);
        }};
#ifndef CELOSTAR
        // TODO(a.swoboda) consider parallelizing this
        std::atomic<group_id_type> index{ctl::cast<group_id_type>(miner_config.eventlog().trace_domain_count().get())};
#endif
        common::for_each_group(element<PICK_TRACE_ID>(view), miner_config.grain_size(), [&](auto interval) {
          const auto interval_last{std::next(view.begin(), interval.end())};
          for (auto it{find_next_start_activity(std::next(view.begin(), interval.begin()), interval_last)}, next_it{it};
               it != interval_last; it = next_it) {
            next_it = find_next_start_activity(it, interval_last);
#ifdef CELOSTAR
            int multiplicity = miner_config.eventlog().get_variant_multiplicity(it->second);
            auto new_trace_id = miner_config.eventlog().add_variant(multiplicity);
            std::for_each(it, next_it, [new_trace_id](auto& p) { p.second = new_trace_id; });
#else
            std::for_each(it, next_it,
                          [idx = index.fetch_add(1, std::memory_order_relaxed)](auto& p) { p.second = idx; });
#endif
          }
        });
      },
      miner_config.eventlog().current_split_eventlog_view());
  miner_config.eventlog().set_trace_domain_count(miner_config.eventlog().trace_domain_count() +
                                                 ctl::cast<row_id>(num_additional_traces));
  return num_additional_traces;
}

}  // namespace

// TODO (goulart.e) this design makes it hard to test the fallbacks
bool slack_tau_loop_fallback::is_applicable(const inductive_miner_config& /*miner_config*/,
                                            const directly_follows_graph& dfg,
                                            const common::execution_context& context) {
  const auto is_applicable_context{context.create_sub_context("slack_tau_loop_fallback::is_applicable", {})};
  return std::ranges::any_of(dfg[boost::graph_bundle].start_vertices, [&dfg](auto p) {
    return !std::ranges::empty(boost::make_iterator_range(boost::in_edges(p.first, dfg)));
  });
}

tau_loop_split_result slack_tau_loop_fallback::apply(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                                     const common::execution_context& context) {
  auto apply_context{context.create_sub_context("slack_tau_loop_fallback::apply", {})};
  return split_log_and_dfg(miner_config, dfg, context);
}

}  // namespace celonis::accelerator::operators::process
