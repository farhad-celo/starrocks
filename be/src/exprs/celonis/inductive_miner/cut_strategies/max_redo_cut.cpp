#include <numeric>

#include <boost/graph/connected_components.hpp>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "../cut_strategy.h"
#include "ctl/algorithm.h"
#include "modules/common/case_aligned_range.h"
#include "modules/common/for_each_group.h"

namespace celonis::accelerator::operators::process {

namespace {

// NB this is actually an overestimate
size_t extra_trace_identifier_count(const directly_follows_graph& dfg, const cut_t& cut) {
  debug_assert(std::accumulate(begin(dfg[boost::graph_bundle].start_vertices),
                               end(dfg[boost::graph_bundle].start_vertices), size_t{0},
                               [](auto acc, auto p) { return acc + p.second; }) ==
               std::accumulate(begin(dfg[boost::graph_bundle].end_vertices), end(dfg[boost::graph_bundle].end_vertices),
                               size_t{0}, [](auto acc, auto p) { return acc + p.second; }));
  // for every edge that crosses from any redo-part to the do-part, we need an extra identifier
  size_t outgoing_count{};
  size_t ingoing_count{};
  for (const auto& edge : boost::make_iterator_range(boost::edges(dfg))) {
    if (cut.second[boost::source(edge, dfg)] != 0 && cut.second[boost::target(edge, dfg)] == 0) {
      ingoing_count += dfg[edge].count;
    }
    if (cut.second[boost::source(edge, dfg)] == 0 && cut.second[boost::target(edge, dfg)] != 0) {
      outgoing_count += dfg[edge].count;
    }
  }
  debug_assert(outgoing_count == ingoing_count);
  return ingoing_count;
}

std::vector<directly_follows_graph> get_sub_dfgs_from_old_dfg(directly_follows_graph const& old_dfg, const cut_t& cut) {
  auto sub_dfgs{sub_dfgs::build_sub_dfgs_from_components(old_dfg, cut, true)};

  // Set the DFG level properties for each sub DFG
  sub_dfgs::add_start_and_end_vertices_from_old_dfg(old_dfg, cut.second, sub_dfgs);
  // NB: Empty trace count 0 for redo cut
  sub_dfgs::accumulate_non_empty_traces_per_dfg(sub_dfgs.sub_dfgs);
  return sub_dfgs.sub_dfgs;
}

}  // namespace

cut_t max_redo_cut::find(const directly_follows_graph& dfg) {
  // #lizard forgives

  // Remember:
  // 1. There is no non-trivial exclusive-choice cut,
  //    i.e. the whole graph is weakly connected.
  // 2. There is no non-trivial sequence cut.
  // 3. There is no non-trivial parallel cut.

  size_t num_vertices{boost::num_vertices(dfg)};
  graph groupings{num_vertices};

  auto const& start_vertices{dfg[boost::graph_bundle].start_vertices};
  auto const& end_vertices{dfg[boost::graph_bundle].end_vertices};

  auto vertices{boost::vertices(dfg)};
  for (auto vertex{vertices.first}; vertex != vertices.second; ++vertex) {
    if (ctl::contains(end_vertices, *vertex)) {
      continue;
    }
    auto neighbours{boost::adjacent_vertices(*vertex, dfg)};
    for (auto neighbour{neighbours.first}; neighbour != neighbours.second; ++neighbour) {
      if (ctl::contains(start_vertices, *neighbour)) {
        continue;
      }
      boost::add_edge(*vertex, *neighbour, groupings);
    }
  }

  // Connect all the start activities to force them into one component,
  // i.e. the body of the redo-loop cut. Note that for each end activity
  // there must be a start activity which is eventually connected to it.
  const size_t body_pivot{start_vertices.begin()->first};
  for (auto it{std::next(start_vertices.begin())}; it != start_vertices.end(); ++it) {
    boost::add_edge(body_pivot, it->first, groupings);
  }
  // Connect all end activities to a start activity. While all end vertices must be reachable through a start vertex,
  // some may only be reachable through other end vertices, so they are not connected yet.
  std::ranges::for_each(end_vertices,
                        [&groupings, body_pivot](auto p) { boost::add_edge(body_pivot, p.first, groupings); });
  // Whenever there is an activity connected to one but not all start activities,
  // this activity must belong to the body.
  if (start_vertices.size() > 1) {
    auto sv_it{start_vertices.begin()};
    const auto& first_predecessors{boost::inv_adjacent_vertices(sv_it->first, dfg)};
    std::unordered_set<size_t> first_validation(first_predecessors.first, first_predecessors.second);
    std::unordered_set<size_t> other_validation{};
    ++sv_it;

    // Check whether all other start activities have no additional neighbours.
    for (; sv_it != start_vertices.end(); ++sv_it) {
      auto other_predecessors{boost::inv_adjacent_vertices(sv_it->first, dfg)};
      for (auto n_it{other_predecessors.first}; n_it != other_predecessors.second; ++n_it) {
        other_validation.insert(*n_it);
        if (ctl::contains(first_validation, *n_it)) {
          continue;
        }
        // `n_it` does not point to a predecessor of the first start activity.
        boost::add_edge(body_pivot, *n_it, groupings);
      }
    }

    // Check whether the first end activity has no additional neighbours.
    for (const auto& neighbour : first_validation) {
      if (ctl::contains(other_validation, neighbour)) {
        continue;
      }
      boost::add_edge(body_pivot, neighbour, groupings);
    }
  }

  // Whenever there is an activity to which one but not all end activities are connected,
  // this activity must belong to the body.
  if (end_vertices.size() > 1) {
    auto ev_it{end_vertices.begin()};
    const auto& first_successors{boost::adjacent_vertices(ev_it->first, dfg)};
    std::unordered_set<size_t> first_validation(first_successors.first, first_successors.second);
    std::unordered_set<size_t> other_validation{};
    ++ev_it;

    // Check whether all other end activities have no additional neighbours.
    for (; ev_it != end_vertices.end(); ++ev_it) {
      auto other_successors{boost::adjacent_vertices(ev_it->first, dfg)};
      for (auto n_it{other_successors.first}; n_it != other_successors.second; ++n_it) {
        other_validation.insert(*n_it);
        if (ctl::contains(first_validation, *n_it)) {
          continue;
        }
        // `n_it` does not point to a successor of the first end activity.
        boost::add_edge(body_pivot, *n_it, groupings);
      }
    }

    // Check whether the first end activity has no additional neighbours.
    for (const auto& neighbour : first_validation) {
      if (ctl::contains(other_validation, neighbour)) {
        continue;
      }
      boost::add_edge(body_pivot, neighbour, groupings);
    }
  }

  std::vector<size_t> component_mapping(num_vertices);
  const auto component_count{boost::connected_components(groupings, component_mapping.data())};

  // Lastly, find the body component and put it first.
  const size_t body_component{component_mapping[body_pivot]};
  for (size_t i{0}; i < num_vertices; ++i) {
    if (component_mapping[i] < body_component) {
      ++component_mapping[i];
    } else if (component_mapping[i] == body_component) {
      component_mapping[i] = 0;
    }
  }

  return cut_t(component_count, component_mapping);
}

max_redo_cut::apply_result max_redo_cut::apply(inductive_miner_config miner_config,
                                               const directly_follows_graph& old_dfg, const cut_t& cut,
                                               common::execution_context& context) {
  const auto apply_context{context.create_sub_context("max_redo_cut::apply", {})};

  // ensure that we have enough space
  const auto extra_id_count{extra_trace_identifier_count(old_dfg, cut)};
  {
    const auto trace_domain_count{miner_config.eventlog.trace_domain_count()};
    miner_config.eventlog = splittable_eventlog::canonicalize_if_necessary(
        std::move(miner_config.eventlog), trace_domain_count + extra_id_count, context);
  }
  // find the mapping of activity ids to dfgs
  const auto activity_to_dfg_mapping{
      cut_strategy::to_activity_dfg_map(cut, old_dfg, miner_config.eventlog.activity_domain_count())};
  const auto get_dfg{[&activity_to_dfg_mapping](const auto& p) { return activity_to_dfg_mapping[p.activity_id()]; }};
  // add extra trace ids
  std::visit(
      [&]<typename VIEW>(const VIEW& view) {
        using trace_id_type = typename eventlog_view_element_t<VIEW>::trace_id_raw_type;
        std::vector<std::atomic<trace_id_type>> extra_ids(cut.first);
        std::ranges::for_each(
            extra_ids, [size = ctl::cast<trace_id_type>(miner_config.eventlog.trace_domain_count().get())](auto& id) {
              id.store(size, std::memory_order_relaxed);
            });
        common::for_each_group(element<PICK_TRACE_ID>(view), miner_config.grain_size, [&](auto interval) {
          ctl::dynamic_bitset sublog_iteration_counts(cut.first, false);
          const auto first{std::next(view.begin(), interval.begin())};
          const auto last{std::next(view.begin(), interval.end())};
          // find beginning of next sublog
          for (auto it{first}, next{it}; it != last; it = next) {
            next = std::ranges::mismatch(it, last, std::next(it), last, std::ranges::equal_to{}, get_dfg, get_dfg).in2;
            if (const auto dfg_index{get_dfg(*it)}; sublog_iteration_counts.test(dfg_index)) {
              // fill trace id with new value
              std::for_each(it, next, [index = extra_ids[dfg_index]++](auto& p) { p.second = index; });
            } else {
              sublog_iteration_counts.set(dfg_index);
            }
          }
        });
        miner_config.eventlog.set_trace_domain_count(
            ctl::cast<row_id>(std::ranges::max_element(extra_ids, [](const auto& lhs, const auto& rhs) {
                                return lhs.load(std::memory_order_relaxed) < rhs.load(std::memory_order_relaxed);
                              })->load(std::memory_order_relaxed)));
      },
      miner_config.eventlog.current_split_eventlog_view());
  auto sub_eventlogs{miner_config.eventlog.split(activity_to_dfg_mapping)};
  debug_assert(sub_eventlogs.size() == cut.first);
  auto dfgs{get_sub_dfgs_from_old_dfg(old_dfg, cut)};
  debug_assert(
      dfgs.front()[boost::graph_bundle].log.trace_count ==
      old_dfg[boost::graph_bundle].log.trace_count +
          std::accumulate(std::next(begin(dfgs)), end(dfgs), process_tree::count_type{0},
                          [](auto acc, const auto& dfg) { return acc + dfg[boost::graph_bundle].log.trace_count; }));
  return {std::move(sub_eventlogs), std::move(dfgs)};
}

process_tree::redo max_redo_cut::from_dfgs(inductive_miner_config miner_config,
                                           max_redo_cut::apply_result logs_and_dfgs, common::execution_context& context,
                                           inductive_miner_statistics& miner_statistics) {
  process_tree::redo result{};
  result.child_redo_counts.resize(logs_and_dfgs.dfgs.size());
  std::ranges::transform(logs_and_dfgs.dfgs, begin(result.child_redo_counts),
                         [](const auto& dfg) { return dfg[boost::graph_bundle].log.trace_count; });
  result.children.resize(result.child_redo_counts.size());
  std::ranges::transform(logs_and_dfgs.eventlogs, logs_and_dfgs.dfgs, begin(result.children),
                         [&](auto& log, auto& dfg) {
                           miner_config.eventlog = std::move(log);
                           return inductive_miner_recurse(miner_config, dfg, context, miner_statistics);
                         });
  if (!result.child_redo_counts.empty()) {
    // Since the number of repetitions of the first child is the sum of the counts of the other children,
    // the object count is the first child's count minus all the other children's counts
    const auto num_redos{std::accumulate(std::next(begin(result.child_redo_counts)), end(result.child_redo_counts),
                                         process_tree::count_type{0})};
    debug_assert(num_redos < result.child_redo_counts.front());
    result.object_count = result.child_redo_counts.front() - num_redos;
  }
  return result;
}

}  // namespace celonis::accelerator::operators::process
