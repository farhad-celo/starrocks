#ifdef CELOSTAR
#include <boost/dynamic_bitset.hpp>
#endif

#include "../fallback_strategy.h"
#include "modules/common/for_each_group.h"

namespace celonis::accelerator::operators::process {

namespace {

bool possible_activities_in_dfg(const directly_follows_graph& dfg) {
  for (auto [it, last]{boost::vertices(dfg)}; it != last; ++it) {
    if (dfg[*it].count == dfg[boost::graph_bundle].log.trace_count) {
      return true;
    }
  }
  return false;
};
}  // namespace

cut_t activity_once_per_trace::find(const inductive_miner_config& miner_config, const directly_follows_graph& dfg,
                                    const common::execution_context& context) {
  const auto find_context{context.create_sub_context("activity_once_per_trace::find", {})};
  const size_t vertex_count{boost::num_vertices(dfg)};
  cut_t ret;
  std::vector<size_t> component_mapping(vertex_count, 0);  // All vertices in one component
  if (possible_activities_in_dfg(dfg)) {
    const auto activity_count{miner_config.eventlog.activity_domain_count()};
    // Create mapping activity `id ~> dfg vertex id`
    std::vector<size_t> activity_dfg_mapping(activity_count);
#ifdef CELOSTAR
    boost::dynamic_bitset<> considered_activities{ctl::cast_unsigned(activity_count.get())};
#else
    ctl::dynamic_bitset_t considered_activities{ctl::cast_unsigned(activity_count.get())};
#endif
    for (size_t vertex_idx{0}; vertex_idx < vertex_count; ++vertex_idx) {
      const auto activity_id = dfg[vertex_idx].activity_id;
      activity_dfg_mapping[activity_id] = vertex_idx;
      considered_activities.set(activity_id);
    }

    auto activity_occurs_exactly_once{std::visit(
        [activity_count](auto view) {
          if (view.empty()) {
#ifdef CELOSTAR
            return boost::dynamic_bitset<>(activity_count);
#else
            return ctl::dynamic_bitset_t(activity_count, false);
#endif
          }
#ifdef CELOSTAR
          boost::dynamic_bitset<> occurs_exactly_once(activity_count);
          occurs_exactly_once.set();
          boost::dynamic_bitset<> activity_occurs_once(activity_count);
          boost::dynamic_bitset<> activity_occurs_twice(activity_count);
#else
          ctl::dynamic_bitset_t occurs_exactly_once(activity_count, true);
          ctl::dynamic_bitset_t activity_occurs_once(activity_count, false);
          ctl::dynamic_bitset_t activity_occurs_twice(activity_count, false);
#endif
          common::for_each_group_stopping(element<PICK_TRACE_ID>(view), [&](auto interval) {
            activity_occurs_once.reset();
            activity_occurs_twice.reset();
            std::ranges::for_each_n(
                std::next(view.begin(), interval.begin()), interval.size(),
                [&](auto a) {
                  if (activity_occurs_once.test_set(a)) {
                    activity_occurs_twice.set(a);
                  }
                },
                [](const auto& p) { return p.activity_id(); });
            occurs_exactly_once &= (activity_occurs_once ^= activity_occurs_twice);
            return occurs_exactly_once.any();
          });
          return occurs_exactly_once;
        },
        miner_config.eventlog.current_split_eventlog_view())};

    auto candidate{(activity_occurs_exactly_once & considered_activities).find_first()};

#ifdef CELOSTAR
    if (candidate != boost::dynamic_bitset<>::npos) {
#else
    if (candidate != ctl::dynamic_bitset_t::npos) {
#endif
      component_mapping[activity_dfg_mapping[candidate]] = 1;  // Put candidate in its own component
      ret = {2, component_mapping};
    } else {
      ret = {1, component_mapping};
    }
  } else {
    ret = {1, component_mapping};
  }
  return ret;
}

activity_once_per_trace::apply_result activity_once_per_trace::apply(inductive_miner_config& miner_config,
                                                                     const directly_follows_graph& old_dfg,
                                                                     const cut_t& cut,
                                                                     common::execution_context& context) {
  // Log splitting/DFG construction for this fallthrough behaves the same as in a parallel cut
  // TODO(j.kruska) CPL-7902 Does it really? Is the max_par_cut mathematically guaranteed to split correctly for this
  //  fallthrough?
  static const max_par_cut strategy{};
  return strategy.apply(miner_config, old_dfg, cut, context);
};

[[nodiscard]] process_tree::parallel activity_once_per_trace::from_dfgs(const inductive_miner_config& miner_config,
                                                                        apply_result dfgs,
                                                                        common::execution_context& context,
                                                                        inductive_miner_statistics& miner_statistics) {
  // This fallthrough puts components in parallel, reuse max_par_cut PT construction
  return max_par_cut::from_dfgs(miner_config, std::move(dfgs), context, miner_statistics);
};

}  // namespace celonis::accelerator::operators::process
