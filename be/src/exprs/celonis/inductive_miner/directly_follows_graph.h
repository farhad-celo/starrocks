#pragma once

#include <unordered_map>
#include <vector>

#ifndef CELOSTAR
#include <bytell_hash_map.hpp>
#endif

#include "modules/common/execution_context_fwd.h"
#include "modules/common/shared_types.h"
#ifdef CELOSTAR
#include "inductive_miner/directly_follows_graph_fwd.h"
#include "inductive_miner/splittable_eventlog_fwd.h"
#include "util/phmap/phmap.h"
#else
#include "modules/cube/filter_bitset_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/operators/process/inductive_miner/directly_follows_graph_fwd.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_fwd.h"
#endif

namespace celonis::accelerator::operators::process {

/**
 * This struct stores all the details on how infrequent behavior should be filtered.
 */
struct dfg_filter_config {
  /**
   * Stores whether start and end activities should be filtered at all.
   */
  bool vertices{false};
  /**
   * Stores the threshold to be used for the start and end activities.
   */
  cel_float_t vertices_threshold{0.};

  /**
   * Inside the base case of a noise affected single activity, `noisy_single_activity_base_case`,
   * this is an upper bound on the average number of occurences (of a single activity) per trace,
   * so that the activity will be considered as a true singleton. Remember: empty traces will be
   * removed before this average is computed.
   */
  cel_float_t vertices_max_avg_occurences{1.};

  /**
   * Stores whether the successions should be filtered at all.
   */
  bool edges{false};
  /**
   * Stores the threshold to be used for the successions.
   */
  cel_float_t edges_threshold{0.};
  /**
   * Stores whether the count of an activity being the end activity of a trace
   * should be included in the maximum of the cardinalities of the out edges.
   */
  bool edges_consider_end_activities{true};
};

/**
 * boost::adjacency list only supports vector and unordered_map which are too slow to aggregate the edges
 * We use flat hashmaps to pre-aggregate the statistics. The DFG is built in a final build step.
 */
struct dfg_pre_aggregation {
  using edge_pair = std::pair<row_id, row_id>;

  struct edge_pair_hash {
    [[nodiscard]] inline size_t operator()(const edge_pair& key) const noexcept {
      std::size_t hash{};
      boost::hash_combine(hash, key.first);
      boost::hash_combine(hash, key.second);
      return hash;
    }
  };

  struct activity_counters {
    size_t frequency_count{};
    size_t start_count{};
    size_t end_count{};
  };

  explicit dfg_pre_aggregation(row_id distinct_activity_count) : activity_statistics(distinct_activity_count) {}

  void add_edge(const row_id from, const row_id to, const size_t edge_multiplicity) {
    const edge_pair insert_key{from, to};
    edge_statistics[insert_key] += edge_multiplicity;
  }

  std::vector<activity_counters> activity_statistics;
#ifdef CELOSTAR
  phmap::flat_hash_map<edge_pair, size_t, edge_pair_hash> edge_statistics;
#else
  ska::bytell_hash_map<edge_pair, size_t, edge_pair_hash> edge_statistics;
#endif
  dfg_log_properties log_properties{};
};

/** Reduces a set of pre-aggregations from multiple threads referring to the same DFG into a single pre aggregation */
dfg_pre_aggregation reduce_pre_aggregates(std::vector<dfg_pre_aggregation>&& dfg_pre_aggs);

namespace dfg {

/** Consumes the pre-aggregation object and generate a DFG */
[[nodiscard]] directly_follows_graph build_dfg(dfg_pre_aggregation&& dfg_pre_agg);

/**
 * Converts an event-log to a directly-follows graph
 * @param eventlog_data the eventlog to convert
 * @param grain_size the grain size to use for parallelization
 * @return the directly-follows graph corresponding to
 */
directly_follows_graph initialize_dfg(const splittable_eventlog& eventlog_data, common::execution_context& context,
                                      size_t grain_size = 1 << 17);

/**
 * This function may be used to remove noise out of the start and end activities
 * of a directly-follows graph. Every (start|end) activity that occurs less than
 * `filter_config.vertices_threshold * max()` times gets removed.
 *
 * @param map
 * @param filter_config
 */
#ifdef CELOSTAR
void filter_dfg_count_map(phmap::flat_hash_map<size_t, size_t>& map,
                          const dfg_filter_config& filter_config = dfg_filter_config());
#else
void filter_dfg_count_map(ska::bytell_hash_map<size_t, size_t>& map,
                          const dfg_filter_config& filter_config = dfg_filter_config());
#endif

/**
 * This function may be used to remove noise out of the successions of a directly-
 * follows graph. Every outgoing edge of a particular activity that occurs less than
 * `filter_config.edges_threshold * max()` times gets removed. `max()` is computed
 * among all the outgoing edges of the same activity.
 *
 * The function always returns a valid DFG, i.e. all nodes are on a path from a start to an end node.
 * If the DFG is not valid after filtering, it is fixed by adding edges according to an strategy.
 *
 * @param dfg
 * @param filter_config
 * @return the filtered DFG
 */
directly_follows_graph filter_dfg_edges(const directly_follows_graph& dfg,
                                        const dfg_filter_config& filter_config = dfg_filter_config());
}  // namespace dfg

}  // namespace celonis::accelerator::operators::process
