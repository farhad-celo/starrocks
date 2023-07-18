#ifdef CELOSTAR
#include <boost/dynamic_bitset.hpp>
#endif
#include <cstddef>
#include <iterator>
#include <vector>

#include "../fallback_strategy.h"
#include "ctl/conversion.h"
#include "modules/common/for_each_group.h"
#ifndef CELOSTAR
#include "modules/cube/filter_bitset.h"
#include "modules/memory/column.h"
#endif

namespace celonis::accelerator::operators::process {

namespace {

struct exec_is_applicable {
#ifdef CELOSTAR
  const boost::dynamic_bitset<>& start_activity_flags_;
  const boost::dynamic_bitset<>& end_activity_flags_;
#else
  const ctl::dynamic_bitset<>& start_activity_flags_;
  const ctl::dynamic_bitset<>& end_activity_flags_;
#endif

#ifdef CELOSTAR
  exec_is_applicable(const boost::dynamic_bitset<>& start_activity_flags, const boost::dynamic_bitset<>& end_activity_flags)
#else
  exec_is_applicable(const ctl::dynamic_bitset<>& start_activity_flags, const ctl::dynamic_bitset<>& end_activity_flags)
#endif
      : start_activity_flags_{start_activity_flags}, end_activity_flags_{end_activity_flags} {}

  template <typename EVENTLOG>
  bool operator()(EVENTLOG eventlog) const {
    const auto is_start_activity{[this](auto activity) { return start_activity_flags_.test(activity); }};
    const auto is_end_activity{[this](auto activity) { return end_activity_flags_.test(activity); }};

    return std::adjacent_find(eventlog.begin(), eventlog.end(), [&](auto lhs, auto rhs) {
             return is_end_activity(lhs.activity_id()) && is_start_activity(rhs.activity_id()) &&
                    lhs.trace_id().get() == rhs.trace_id().get();  // comparison on raw values due to C++20 issue
           }) != eventlog.end();
  }
};

size_t split_dfg_strict_tau_style(directly_follows_graph& dfg) {
  size_t result{};
  for (auto& [start_vertex, start_count] : dfg[boost::graph_bundle].start_vertices) {
    for (auto& [end_vertex, end_count] : dfg[boost::graph_bundle].end_vertices) {
      auto [edge, is_present]{boost::edge(end_vertex, start_vertex, dfg)};
      if (is_present) {
        result += dfg[edge].count;
        start_count += dfg[edge].count;
        end_count += dfg[edge].count;
        boost::remove_edge(edge, dfg);
      }
    }
  }
  dfg[boost::graph_bundle].log.trace_count += result;
  return result;
}

size_t apply_internal(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                      common::execution_context& context) {
  // compute the number of additional sub-traces and already split the dfg
  const auto num_additional_traces{split_dfg_strict_tau_style(dfg)};
#ifdef CELOSTAR
  // In Celostar, we use a fixed 32bit space.
#else
  // ensure that our event-log has enough "space" to accommodate additional group ids
  miner_config.eventlog =
      splittable_eventlog::canonicalize_if_necessary(std::move(miner_config.eventlog), num_additional_traces, context);
#endif
  std::visit(
      [&miner_config, &dfg](auto view) {
        using activity_id_type = typename std::decay_t<decltype(view.front())>::first_type;
        using group_id_type = typename std::decay_t<decltype(view.front())>::second_type;
        std::set<activity_id_type> start_activities{};
        std::ranges::transform(dfg[boost::graph_bundle].start_vertices,
                               std::inserter(start_activities, end(start_activities)),
                               [&dfg](auto p) { return dfg[p.first].activity_id; });
        std::set<activity_id_type> end_activities{};
        std::ranges::transform(dfg[boost::graph_bundle].end_vertices,
                               std::inserter(end_activities, end(end_activities)),
                               [&dfg](auto p) { return dfg[p.first].activity_id; });
        const auto is_end_start_edge{[&](const auto& fst, const auto& snd) {
          return end_activities.contains(fst.activity_id_raw()) && start_activities.contains(snd.activity_id_raw());
        }};
        const auto get_next_start{[&](auto first, auto last) {
          auto it{std::adjacent_find(first, last, is_end_start_edge)};
          // std::adjacent_find returns a pointer to the first element of the pair or the past-the-end iterator.
          // In the first case, we need to adjust it to get the correct range
          if (it != last) {
            ++it;
          }
          return it;
        }};
#ifndef CELOSTAR
        // TODO(a.swoboda) consider parallelizing this
        std::atomic<group_id_type> index{ctl::cast<group_id_type>(miner_config.eventlog.trace_domain_count().get())};
#endif
        common::for_each_group(element<PICK_TRACE_ID>(view), miner_config.grain_size, [&](auto interval) mutable {
          // find adjacent end and start indices, and fill all the following elements with a new index
          const auto interval_last{std::next(view.begin(), interval.end())};
          for (auto it{get_next_start(std::next(view.begin(), interval.begin()), interval_last)}, next_it{it};
               it != interval_last; it = next_it) {
            next_it = get_next_start(it, interval_last);
#ifdef CELOSTAR
            int multiplicity = miner_config.eventlog.get_variant_multiplicity(it->second);
            auto new_trace_id = miner_config.eventlog.add_variant(multiplicity);
            std::for_each(it, next_it, [new_trace_id](auto& p) { p.second = new_trace_id; });
#else
            std::for_each(it, next_it,
                          [idx = index.fetch_add(1, std::memory_order_relaxed)](auto& p) { p.second = idx; });
#endif
          }
        });
      },
      miner_config.eventlog.current_split_eventlog_view());
  miner_config.eventlog.set_trace_domain_count(miner_config.eventlog.trace_domain_count() +
                                               ctl::cast<row_id>(num_additional_traces));
  return num_additional_traces;
}

}  // namespace

bool strict_tau_loop_fallback::is_applicable(const inductive_miner_config& miner_config,
                                             const directly_follows_graph& dfg,
                                             const common::execution_context& context) {
#ifdef CELOSTAR
  boost::dynamic_bitset<> start_activities(miner_config.eventlog.activity_domain_count());
#else
  ctl::dynamic_bitset start_activities(miner_config.eventlog.activity_domain_count(), false);
#endif
  std::ranges::for_each(dfg[boost::graph_bundle].start_vertices,
                        [&](auto v) { start_activities.set(dfg[v.first].activity_id); });
  const auto is_applicable_context{context.create_sub_context("strict_tau_loop_fallback::is_applicable", {})};
#ifdef CELOSTAR
  boost::dynamic_bitset<> end_activities(miner_config.eventlog.activity_domain_count());
#else
  ctl::dynamic_bitset end_activities(miner_config.eventlog.activity_domain_count(), false);
#endif
  std::ranges::for_each(dfg[boost::graph_bundle].end_vertices,
                        [&](auto v) { end_activities.set(dfg[v.first].activity_id); });
  return std::visit(exec_is_applicable{start_activities, end_activities},
                    miner_config.eventlog.current_split_eventlog_view());
}

tau_loop_split_result strict_tau_loop_fallback::apply(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                                      common::execution_context& context) {
  auto apply_context{context.create_sub_context("strict_tau_loop_fallback::apply", {})};
  const auto redo_count{apply_internal(miner_config, dfg, context)};
  return redo_count;
}

}  // namespace celonis::accelerator::operators::process
