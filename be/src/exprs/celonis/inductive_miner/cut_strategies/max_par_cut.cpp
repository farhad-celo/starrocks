#include <numeric>

#include <boost/graph/connected_components.hpp>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#ifdef CELOSTAR
#include "inductive_miner/cut_strategy.h"
#include "inductive_miner/inductive_miner.h"
#else
#include "ctl/dynamic_bitset.h"
#endif
#include "modules/common/case_aligned_range.h"
#include "modules/common/for_each_group.h"
#ifndef CELOSTAR
#include "modules/operators/process/inductive_miner/cut_strategy.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"
#endif

namespace celonis::accelerator::operators::process {

namespace {

using dfg_vertex_descriptor = boost::graph_traits<directly_follows_graph>::vertex_descriptor;
using graph_vertex_descriptor = boost::graph_traits<directly_follows_graph>::vertex_descriptor;

static_assert(std::is_same_v<dfg_vertex_descriptor, graph_vertex_descriptor>,
              "The code was written with the assumption that both vertex have the same type. "
              "The code might need some changes if this is not the case anymore");

struct connected_component {
  std::unordered_set<graph_vertex_descriptor> dfg_vertices{};
  bool has_start_vertex{false};
  bool has_end_vertex{false};

  [[nodiscard]] bool has_both() const noexcept { return has_start_vertex && has_end_vertex; }

  connected_component& operator+=(const connected_component& rhs) {
    dfg_vertices.insert(std::cbegin(rhs.dfg_vertices), std::cend(rhs.dfg_vertices));
    has_start_vertex = has_start_vertex || rhs.has_start_vertex;
    has_end_vertex = has_end_vertex || rhs.has_end_vertex;
    return *this;
  }
};

}  // namespace

cut_t max_par_cut::find(const directly_follows_graph& dfg) {
  // #lizard forgives

  // Remember:
  // 1. There is no non-trivial exclusive-choice cut,
  //    i.e. the whole graph is weakly connected.
  // 2. There is no non-trivial sequence cut.

  const size_t num_vertices{boost::num_vertices(dfg)};

  // Construct the undirected hull of the complementary graph of dfg.
  // TODO (goulart.e) find Python repository or remove all comments with such references
  // Further explanations can be found in the python-induction-miner repo.
  // TODO (goulart.e) ->undirected<- graph
  graph groupings{num_vertices};

  const auto vertices{boost::vertices(dfg)};
  auto sorted_vertices{std::vector<dfg_vertex_descriptor>(vertices.first, vertices.second)};
  std::sort(sorted_vertices.begin(), sorted_vertices.end());

  // TODO (goulart.e) this is loop is too involved for something rather simple.
  //  Change to a simple loop that tests the existence of each edge
  for (const auto& v : sorted_vertices) {
    // TODO (goulart.e) boost::adjacent_vertices only returns outgoing edges. This might cause bugs (see other cuts too)
    const auto neighbours{boost::adjacent_vertices(v, dfg)};
    std::vector<dfg_vertex_descriptor> sorted_neighbours(neighbours.first, neighbours.second);
    std::sort(sorted_neighbours.begin(), sorted_neighbours.end());
    sorted_neighbours.erase(std::unique(std::begin(sorted_neighbours), std::end(sorted_neighbours)),
                            std::end(sorted_neighbours));

    // Now add all edges that do not occur in the original graph.
    auto vertex{sorted_vertices.begin()};
    auto neighbour{sorted_neighbours.begin()};
    for (; vertex != sorted_vertices.end(); ++vertex) {
      if (neighbour == sorted_neighbours.end() || *vertex < *neighbour) {
        // `vertex` is no neighbour of `v`, thus:
        // violated
        if (v == *vertex) {
          // This check was placed above and was causing the invariant vertex <= neighbour to be violated
          continue;
        }
        boost::add_edge(v, *vertex, groupings);
        // TODO (goulart.e): validate that the access via vertex descriptors of a different graph causes no harm.
      } else {
        // Since both `sorted_neighbours` and `sorted_vertices` are sorted and (!)
        // the neighbours are included in the vertices, `vertex > neighbour` will
        // never occur. Therefore, this case is equivalent to `*vertex == *neighbour`.
        ++neighbour;
      }
    }
  }

  // TODO (goulart.e) this is dangerous. It will probably fail if vertex_descriptors are not continuous integers
  //  Component mapping should rather be a map (apply to other parts of the code)
  using component_id_t = size_t;
  std::vector<component_id_t> component_mapping(num_vertices);
  const auto component_count{boost::connected_components(groupings, component_mapping.data())};

  std::unordered_map<component_id_t, connected_component> connected_components(component_count);
  for (auto [it, last]{boost::vertices(groupings)}; it != last; ++it) {
    const auto tgt_component_id{component_mapping[*it]};
    auto& component{connected_components.try_emplace(tgt_component_id).first->second};
    component.dfg_vertices.insert(*it);
  }

  // Check whether each component contains a start and an end activity.
  {
    const auto& start_vertices{dfg[boost::graph_bundle].start_vertices};
    const auto& end_vertices{dfg[boost::graph_bundle].end_vertices};
    for (const auto& sv : start_vertices) {
      const auto component_id{component_mapping[sv.first]};
      connected_components.at(component_id).has_start_vertex = true;
    }
    for (const auto& ev : end_vertices) {
      const auto component_id{component_mapping[ev.first]};
      connected_components.at(component_id).has_end_vertex = true;
    }
  }

  // Merge some components, so that each has both a start and an end activity.
  // TODO (goulart.e): investigate whether adding a shortcut as in python (cf. repo python-induction-miner) is
  //  beneficial.
  std::vector<component_id_t> needs_start;
  std::vector<component_id_t> needs_end;
  std::vector<component_id_t> needs_both;
  for (const auto& [comp_id, component] : connected_components) {
    if (!component.has_start_vertex && !component.has_end_vertex) {
      needs_both.push_back(comp_id);
    } else if (!component.has_end_vertex) {
      needs_end.push_back(comp_id);
    } else if (!component.has_start_vertex) {
      needs_start.push_back(comp_id);
    }
  }

  // Merge the components, that are missing exactly one type of activity.
  auto needs_start_it{std::cbegin(needs_start)};
  auto needs_end_it{std::cbegin(needs_end)};
  for (; needs_start_it != std::cend(needs_start) && needs_end_it != std::cend(needs_end);
       ++needs_start_it, ++needs_end_it) {
    auto& needs_start_comp{connected_components.at(*needs_start_it)};
    auto& needs_end_comp{connected_components.at(*needs_end_it)};
    needs_start_comp += needs_end_comp;
    connected_components.erase(*needs_end_it);
  }

  // TODO (goulart.e) this merge step seems to be under-specified.
  //  ProM and Leemans' thesis indicates that the split can be "fixed",
  //    i.e. ensure that all components have a start and end node by arbitrarily merging components
  //  ProM performs the merge as our old code used to:
  //    "zips" the needs_start/end components
  //    Then remaining needs_starts/needs_end components are merged into component at index 0,
  //      which can be any component, since the order is not fixed
  //    The remaining components (that need both) are also merged into component at index 0
  //  PM4Py sorts the components by size and merge components "sequentially" until they contain start and end nodes
  //    For example, with components [{1}, {2}, {3, 4}, {5}, {6}] where start_nodes = [1,3,5] and end_nodes=[2,4,6]
  //      we first merge {1} and {2}, that yields {1,2}, which is complete
  //      component {3,4} is also complete
  //      component {5} is missing an end node and will be merged with {3,4} => {3,4,5}
  //      similarly, {6} is missing a start node and will be merged with {3,4,5}
  //  Curiously, ProM and PM4Py end up producing the same split.
  //  Which could be a coincidence, or it could be that ProM is actually implementing some sort ordering somewhere else
  //  We follow PM4Py's approach as it's (a bit more) deterministic. But we need to investigate this further
  std::vector<connected_component> cut_components{};
  for (auto& [comp_id, component] : connected_components) {
    cut_components.push_back(std::move(component));
  }

  std::sort(std::begin(cut_components), std::end(cut_components),
            [](const auto& lhs, const auto& rhs) { return lhs.dfg_vertices.size() < rhs.dfg_vertices.size(); });

  std::vector<connected_component> output_components{};
  auto cut_comp_start{std::begin(cut_components)};
  auto cut_comp_end{std::end(cut_components)};
  output_components.push_back(*cut_comp_start);

  for (auto cut_comp_it{std::next(cut_comp_start)}; cut_comp_it != cut_comp_end; ++cut_comp_it) {
    if (output_components.back().has_both() && cut_comp_it->has_both()) {
      output_components.push_back(*cut_comp_it);
    } else {
      output_components.back() += *cut_comp_it;
    }
  }

  for (component_id_t comp_id{0}; comp_id < output_components.size(); ++comp_id) {
    for (const auto& vertex_desc : output_components.at(comp_id).dfg_vertices) {
      component_mapping.at(vertex_desc) = comp_id;
    }
  }

  return cut_t(output_components.size(), component_mapping);
}

max_par_cut::apply_result max_par_cut::apply(inductive_miner_config& miner_config,
                                             const directly_follows_graph& old_dfg, const cut_t& cut,
                                             common::execution_context& context) {
  auto dfgs{apply_dfgs(miner_config, old_dfg, cut, context)};
  return {apply_split(miner_config, old_dfg, cut, context), std::move(dfgs)};
}

std::vector<directly_follows_graph> max_par_cut::apply_dfgs(const inductive_miner_config& miner_config,
                                                            const directly_follows_graph& old_dfg, const cut_t& cut,
                                                            common::execution_context& /*context*/) {
  const auto activity_to_dfg_mapping{
      cut_strategy::to_activity_dfg_map(cut, old_dfg, miner_config.eventlog.activity_domain_count())};
  tbb::enumerable_thread_specific<std::vector<dfg_pre_aggregation>> thread_pre_aggs(
      cut.first, dfg_pre_aggregation{miner_config.eventlog.activity_domain_count()});
  std::visit(
      [&]<typename VIEW>(const VIEW& view) {
        using activity_type = typename eventlog_view_element_t<VIEW>::activity_id_raw_type;

        common::for_each_group(
            element<PICK_TRACE_ID>(view), miner_config.grain_size,
            [&, last_activities = std::vector<activity_type>(cut.first)](auto interval, auto& local_pre_aggs) mutable {
              std::ranges::fill(last_activities, 0);
              for (auto idx{interval.begin()}; idx != interval.end(); ++idx) {
                const auto current_activity{view[idx].activity_id_raw()};
                const auto dfg_idx{activity_to_dfg_mapping[view[idx].activity_id()]};
                auto& pre_agg{local_pre_aggs[dfg_idx]};
                auto& last_activity{last_activities[dfg_idx]};
                if (last_activity == 0) {  // this is the first activity in this subgraph
                  pre_agg.activity_statistics[current_activity].start_count += 1;
                } else {  // add edge
                  pre_agg.add_edge(static_cast<row_id>(last_activity), static_cast<row_id>(current_activity), 1);
                }
                pre_agg.activity_statistics[current_activity].frequency_count += 1;
                last_activity = current_activity;
              }
              // set trace counts, end activities, and empty traces
              for (size_t dfg_idx{0}; dfg_idx != cut.first; ++dfg_idx) {
                auto& pre_agg{local_pre_aggs[dfg_idx]};
                pre_agg.log_properties.trace_count += 1;
                if (const auto end_activity{last_activities[dfg_idx]}; end_activity == 0) {
                  pre_agg.log_properties.contains_empty_trace += 1;
                } else {
                  pre_agg.activity_statistics[end_activity].end_count += 1;
                }
              }
            },
            thread_pre_aggs);
      },
      miner_config.eventlog.current_split_eventlog_view());
  // "transpose" the thread local pre-aggregations
  std::vector<std::vector<dfg_pre_aggregation>> pre_aggs(cut.first);
  std::ranges::for_each(thread_pre_aggs, [&](auto& local_pre_aggs) {
    for (size_t i{0}; i != cut.first; ++i) {
      pre_aggs[i].emplace_back(std::move(local_pre_aggs[i]));
    }
  });
  std::vector<dfg_pre_aggregation> pre_result{};
  std::ranges::transform(pre_aggs, std::back_inserter(pre_result),
                         [](auto& vec) { return reduce_pre_aggregates(std::move(vec)); });
  std::vector<directly_follows_graph> result(cut.first);
  std::ranges::transform(pre_result, begin(result), [](auto& pre_agg) { return dfg::build_dfg(std::move(pre_agg)); });
  debug_assert(
      std::ranges::all_of(result, [trace_count = old_dfg[boost::graph_bundle].log.trace_count](const auto& dfg) {
        return trace_count == dfg[boost::graph_bundle].log.trace_count;
      }));
  return result;
}

std::vector<splittable_eventlog> max_par_cut::apply_split(inductive_miner_config& miner_config,
                                                          const directly_follows_graph& old_dfg, const cut_t& cut,
                                                          common::execution_context& context) {
  auto sub_eventlog_context{context.create_sub_context("max_par_cut: compute sub-eventlogs", {})};
  const auto activity_count{miner_config.eventlog.activity_domain_count()};
  // create mapping from activity id to dfg id.
  const auto activity_dfg_mapping{cut_strategy::to_activity_dfg_map(cut, old_dfg, activity_count)};
  return miner_config.eventlog.split(activity_dfg_mapping);
}

process_tree::parallel max_par_cut::from_dfgs(inductive_miner_config miner_config, apply_result logs_and_dfgs,
                                              common::execution_context& context,
                                              inductive_miner_statistics& miner_statistics) {
  auto& [logs, dfgs]{logs_and_dfgs};
  debug_assert(logs.size() == dfgs.size());
  debug_assert(!dfgs.empty());
  process_tree::parallel result{{}, dfgs.front()[boost::graph_bundle].log.trace_count};
  result.children.resize(dfgs.size());
  std::ranges::transform(logs, dfgs, begin(result.children), [&](auto& log, auto& dfg) {
    debug_assert(dfg[boost::graph_bundle].log.trace_count == result.object_count);
    const auto cmp_dfg{dfg::initialize_dfg(log, context)};
    debug_assert(cmp_dfg[boost::graph_bundle].log.trace_count ==
                 dfg[boost::graph_bundle].log.trace_count - dfg[boost::graph_bundle].log.contains_empty_trace);
    miner_config.eventlog = std::move(log);
    return inductive_miner_recurse(miner_config, dfg, context, miner_statistics);
  });
  return result;
}

}  // namespace celonis::accelerator::operators::process
