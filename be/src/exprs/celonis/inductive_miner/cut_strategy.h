#pragma once

#include <utility>
#include <vector>

#include <boost/graph/adjacency_list.hpp>

#ifdef CELOSTAR
#include "inductive_miner/directly_follows_graph.h"
#include "inductive_miner/inductive_miner.h"
#include "modules/common/execution_context.h"
#else
#include "modules/operators/process/inductive_miner/directly_follows_graph.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"
#endif

namespace celonis::accelerator::operators::process {

struct slack_tau_loop_fallback;
struct strict_tau_loop_fallback;

// simple undirected graph used to compute groupings
using graph = boost::adjacency_list<boost::hash_setS, boost::vecS, boost::undirectedS>;
using cut_t = std::pair<size_t, std::vector<size_t>>;

class cut_strategy {
 public:
  struct apply_result {
    std::vector<splittable_eventlog> eventlogs{};
    std::vector<directly_follows_graph> dfgs{};
  };
  using activity_dfg_map = std::vector<size_t>;
  static activity_dfg_map to_activity_dfg_map(const cut_t& cut, const directly_follows_graph& dfg,
                                              row_id activity_count) {
    const auto dfg_count{cut.first};
    const auto& vertex_to_dfg{cut.second};
    activity_dfg_map result(activity_count, dfg_count);
    for (auto vertex : boost::make_iterator_range(boost::vertices(dfg))) {
      const auto activity{dfg[vertex].activity_id};
      result[activity] = vertex_to_dfg[vertex];
    }
    return result;
  }
};

/**
 * maximal exclusive-choice cut
 */
class max_xor_cut : public cut_strategy {
 public:
  explicit max_xor_cut() = default;

  [[nodiscard]] static cut_t find(const directly_follows_graph& dfg);

  [[nodiscard]] static apply_result apply(inductive_miner_config& miner_config, const directly_follows_graph& old_dfg,
                                          const cut_t& cut, const common::execution_context& context,
                                          const cube::execution::tracking::stop_token& stop_token);

  [[nodiscard]] static process_tree::exclusive from_dfgs(inductive_miner_config miner_config, apply_result dfgs,
                                                         const common::execution_context& context,
                                                         inductive_miner_statistics& miner_statistics,
                                                         const cube::execution::tracking::stop_token& stop_token);
};

/**
 * noisy exclusive-choice cut, splits deviating traces to conform to the cut found on the filtered DFG
 *
 * If we find an XOR cut on an edge-filtered DFG, we have to handle deviating traces. For example, when applying the
 * cut (X, {a, b}, {c}) to the trace <a, b, c>. Our heuristic makes the trace conformant by deleting as few events as
 * possible. That is, we only keep the events from the component that occurs the most in the trace. In this case, we
 * delete c and keep <a, b>. Note that each trace has at least one event from a component because we enforce that
 * every activity in the filtered DFG is reachable.
 */
class noisy_xor_cut : public max_xor_cut {
 public:
  explicit noisy_xor_cut() = default;

  [[nodiscard]] static apply_result apply(inductive_miner_config& miner_config, const directly_follows_graph& old_dfg,
                                          const cut_t& cut, const common::execution_context& context,
                                          const cube::execution::tracking::stop_token& stop_token);
};

/**
 * maximal sequence cut
 */
class max_seq_cut : public cut_strategy {
 public:
  explicit max_seq_cut() = default;

  [[nodiscard]] static cut_t find(const directly_follows_graph& dfg);

  [[nodiscard]] static apply_result apply(inductive_miner_config& miner_config, const directly_follows_graph& old_dfg,
                                          const cut_t& cut, const common::execution_context& context,
                                          const cube::execution::tracking::stop_token& stop_token);

  [[nodiscard]] static process_tree::sequence from_dfgs(inductive_miner_config miner_config, apply_result dfgs,
                                                        const common::execution_context& context,
                                                        inductive_miner_statistics& miner_statistics,
                                                        const cube::execution::tracking::stop_token& stop_token);
};

/**
 * maximal parallel cut
 */
class max_par_cut : public cut_strategy {
 public:
  explicit max_par_cut() = default;

  [[nodiscard]] static cut_t find(const directly_follows_graph& dfg);

  [[nodiscard]] static apply_result apply(inductive_miner_config& miner_config, const directly_follows_graph& old_dfg,
                                          const cut_t& cut, const common::execution_context& context,
                                          const cube::execution::tracking::stop_token& stop_token);

  [[nodiscard]] static std::vector<directly_follows_graph> apply_dfgs(
      const inductive_miner_config& miner_config, const directly_follows_graph& old_dfg, const cut_t& cut,
      const common::execution_context& context, const cube::execution::tracking::stop_token& stop_token);

  [[nodiscard]] static std::vector<splittable_eventlog> apply_split(
      inductive_miner_config& miner_config, const directly_follows_graph& old_dfg, const cut_t& cut,
      const common::execution_context& context, const cube::execution::tracking::stop_token& stop_token);

  [[nodiscard]] static process_tree::parallel from_dfgs(inductive_miner_config miner_config, apply_result dfgs,
                                                        const common::execution_context& context,
                                                        inductive_miner_statistics& miner_statistics,
                                                        const cube::execution::tracking::stop_token& stop_token);
};

/**
 * maximal redo-loop cut
 */
class max_redo_cut : public cut_strategy {
 public:
  explicit max_redo_cut() = default;

  [[nodiscard]] static cut_t find(const directly_follows_graph& dfg);

  [[nodiscard]] static apply_result apply(inductive_miner_config miner_config, const directly_follows_graph& old_dfg,
                                          const cut_t& cut, const common::execution_context& context,
                                          const cube::execution::tracking::stop_token& stop_token);

  [[nodiscard]] static process_tree::redo from_dfgs(inductive_miner_config miner_config, apply_result dfgs,
                                                    const common::execution_context& context,
                                                    inductive_miner_statistics& miner_statistics,
                                                    const cube::execution::tracking::stop_token& stop_token);
};

template <class CUT_STRATEGY>
cut_t find_cut(const directly_follows_graph& dfg) {
  static const CUT_STRATEGY strategy{};
  return strategy.find(dfg);
}

template <class CUT_STRATEGY>
cut_t find_cut(const inductive_miner_config& miner_config, const directly_follows_graph& dfg,
               const common::execution_context& context, const cube::execution::tracking::stop_token& stop_token) {
  static const CUT_STRATEGY strategy{};
  return strategy.find(miner_config, dfg, context, stop_token);
}

template <typename CUT_STRATEGY>
process_tree apply_cut_composite(const cut_t& cut, inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                 const common::execution_context& context, inductive_miner_statistics& miner_statistics,
                                 const cube::execution::tracking::stop_token& stop_token) {
  static const CUT_STRATEGY strategy{};
  auto new_dfgs{strategy.apply(miner_config, dfg, cut, context, stop_token)};

  return {{CUT_STRATEGY::from_dfgs(miner_config, new_dfgs, context, miner_statistics, stop_token)}};
}

template <>
inline process_tree apply_cut_composite<max_redo_cut>(const cut_t& cut, inductive_miner_config& miner_config,
                                                      directly_follows_graph& dfg,
                                                      const common::execution_context& context,
                                                      inductive_miner_statistics& miner_statistics,
                                                      const cube::execution::tracking::stop_token& stop_token) {
  auto new_dfgs{max_redo_cut{}.apply(miner_config, dfg, cut, context, stop_token)};

  return {max_redo_cut::from_dfgs(miner_config, new_dfgs, context, miner_statistics, stop_token)};
}

template <typename T>
concept TAU_LOOP = std::is_same_v<T, slack_tau_loop_fallback> || std::is_same_v<T, strict_tau_loop_fallback>;

template <TAU_LOOP CUT_STRATEGY>
process_tree apply_cut_composite(inductive_miner_config miner_config, directly_follows_graph& dfg,
                                 const common::execution_context& context, inductive_miner_statistics& miner_statistics,
                                 const cube::execution::tracking::stop_token& stop_token) {
  // copy miner_config in order to not propagate changes beyond this cut's children
  static const CUT_STRATEGY strategy{};
  auto tau_loop_result{strategy.apply(miner_config, dfg, context)};
  return {CUT_STRATEGY::from_dfgs(miner_config, dfg, tau_loop_result, context, miner_statistics, stop_token)};
}

namespace sub_dfgs {

/**
 * Stores the sub DFGs together with a mapping from the old node id to the new node id in the sub DFG
 */
struct sub_dfgs_t {
  std::vector<directly_follows_graph> sub_dfgs;
  std::vector<size_t> old_vertex_to_new_vertex;

  sub_dfgs_t(std::vector<directly_follows_graph> sub_dfgs, std::vector<size_t> old_vertex_to_new_vertex)
      : sub_dfgs(std::move(sub_dfgs)), old_vertex_to_new_vertex(std::move(old_vertex_to_new_vertex)) {}
};

/**
 * Constructs the sub DFGs nodes and edges from the old DFG, given the cut
 * @param old_dfg
 * @param cut
 * @param add_counts_for_inter_component_edges If true, handle inter component edges by making them end / start vertices
 * @return the sub DFGs alongside the mapping from the old node id to the new node id
 */
[[nodiscard]] sub_dfgs_t build_sub_dfgs_from_components(const directly_follows_graph& old_dfg, const cut_t& cut,
                                                        bool add_counts_for_inter_component_edges);

/**
 * Adds the start and end vertices from the old DFG to the DFG level property of the corresponding sub DFG alongside
 * their counts
 * @param old_dfg
 * @param vertex_to_component
 * @param sub_graphs
 */
void add_start_and_end_vertices_from_old_dfg(const directly_follows_graph& old_dfg,
                                             const std::vector<size_t>& vertex_to_component, sub_dfgs_t& sub_graphs);

/**
 * Accumulates the non-empty trace count per sub DFG in the trace_count property of the input DFG by adding the counts
 * of the start vertices of each sub DFG. Result is stored in the trace_count property of each sub DFG
 * @param sub_graphs
 */
void accumulate_non_empty_traces_per_dfg(std::vector<directly_follows_graph>& sub_dfgs);

}  // namespace sub_dfgs

}  // namespace celonis::accelerator::operators::process
