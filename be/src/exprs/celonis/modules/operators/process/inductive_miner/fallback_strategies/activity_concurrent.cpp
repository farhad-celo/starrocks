#include <algorithm>

#ifdef CELOSTAR
#include "ctl/assert.h"
#endif
#include "modules/operators/process/inductive_miner/base_case_strategy.h"
#include "modules/operators/process/inductive_miner/cut_strategy.h"
#include "modules/operators/process/inductive_miner/fallback_strategy.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"

namespace celonis::accelerator::operators::process {

namespace {

/**
 * Associates a cut type with the key used in the inductive_miner_statistics
 * @tparam CUT The actual type of cut (exclusive, sequence, parallel, or redo)
 */
template <typename CUT>
extern const std::string key;

template <>
const std::string key<max_xor_cut>{inductive_miner_statistics::xor_count_key()};
template <>
const std::string key<max_seq_cut>{inductive_miner_statistics::seq_count_key()};
template <>
const std::string key<max_par_cut>{inductive_miner_statistics::par_count_key()};
template <>
const std::string key<max_redo_cut>{inductive_miner_statistics::loop_count_key()};

}  // namespace

std::vector<activity_concurrent::cut_and_dfgs> activity_concurrent::compute_parallel_dfgs(
    const inductive_miner_config& miner_config, const directly_follows_graph& dfg,
    const common::execution_context& context, const cube::execution::tracking::stop_token& stop_token) {
  const auto num_vertices{boost::num_vertices(dfg)};
  std::vector<cut_and_dfgs> parallel_dfgs(num_vertices);
  std::ranges::transform(
      boost::make_iterator_range(boost::vertices(dfg)), begin(parallel_dfgs), [&, num_vertices](auto par_vertex) {
        std::vector<size_t> component_mapping(num_vertices, 0);  // All vertices in one component
        component_mapping[par_vertex] = 1;                       // Put par_vertex in its own component

        const auto par_cut = cut_t(2, component_mapping);
        return cut_and_dfgs{par_cut, max_par_cut{}.apply_dfgs(miner_config, dfg, par_cut, context, stop_token)};
      });
  return parallel_dfgs;
}

/*
 *  Details about the function implemented below can be found in:
 *    - ProM Reference implementation: class FallThroughActivityConcurrent
 *    - PhD Thesis of Sander Leemans: 6.1 Inductive Miner - Fall Throughs (especially p. 195)
 *
 */

std::optional<process_tree> activity_concurrent::apply_if_applicable(
    inductive_miner_config& miner_config, const directly_follows_graph& dfg, const common::execution_context& context,
    inductive_miner_statistics& miner_statistics, const cube::execution::tracking::stop_token& stop_token) {
  // Naively, one would go through the activities, compute the parallel cut, and then try all regular cuts (exclusive,
  // sequence, parallel, redo), until one is found. However, this may result in finding a sub-optimal cut (e.g., we may
  // find a redo cut, but splitting off another activity may result in a sequence cut, which is usually preferable).
  // That's why we go through the "hierarchy" of cuts (from exclusive to redo), and try splitting off all activities
  // before moving on to the next cut.
  // Alternatively, one could through the activities and try out cuts until we find one. If it is an exclusive cut, we
  // are done; if not, we continue with the remaining activities, but only try the "better" cuts.
  // This will result in finding the same fallthrough cut, but the number of cuts we compute differs.
  // Assuming that the best possible single parallel activity is 'd', and produces a sequence cut, we currently compute
  // the following cuts (marked by 'x'):
  //  ^   redo 0 0 0 0 0 0 0 0 0 0
  //  |    par 0 0 0 0 0 0 0 0 0 0
  // cuts  seq x x x x 0 0 0 0 0 0
  //  |    xor x x x x x x x x x x
  //           -------------------
  //           x x x x x x x x x x  <- parallel cuts
  //           a b c d e f g h i j
  //           --- activities --->
  //
  // In the other case, we may compute something like this:
  //  ^   redo x x 0 0 0 0 0 0 0 0
  //  |    par x x x 0 0 0 0 0 0 0
  // cuts  seq x x x x 0 0 0 0 0 0
  //  |    xor x x x x x x x x x x
  //           -------------------
  //           x x x x x x x x x x  <- parallel cuts
  //           a b c d e f g h i j
  //           --- activities --->
  //
  // which is slightly worse. However, had we discovered an exclusive (instead of a sequence) cut for activity 'b', we
  // would get this:
  //  ^   redo 0 0 0 0 0 0 0 0 0 0
  //  |    par 0 0 0 0 0 0 0 0 0 0
  // cuts  seq 0 0 0 0 0 0 0 0 0 0
  //  |    xor x x 0 0 0 0 0 0 0 0
  //           -------------------
  //           x x x x x x x x x x  <- parallel cuts
  //           a b c d e f g h i j
  //           --- activities --->
  //
  // And in the other case, we may get something like this:
  //  ^   redo x 0 0 0 0 0 0 0 0 0
  //  |    par x 0 0 0 0 0 0 0 0 0
  // cuts  seq x 0 0 0 0 0 0 0 0 0
  //  |    xor x x 0 0 0 0 0 0 0 0
  //           -------------------
  //           x x 0 0 0 0 0 0 0 0  <- parallel cuts
  //           a b c d e f g h i j
  //           --- activities --->
  //
  // which is slightly better now. Without further insights, we can't settle which of these versions is better.
  using cut_impl = std::variant<max_xor_cut, max_seq_cut, max_par_cut, max_redo_cut>;
  const std::vector<cut_impl> cut_implementations{max_xor_cut{}, max_seq_cut{}, max_par_cut{}, max_redo_cut{}};
  stop_token.stop_execution_if_requested();
  auto parallel_dfgs{compute_parallel_dfgs(miner_config, dfg, context, stop_token)};

  const auto compute_result{[&]<typename IMPL>(IMPL /**/, cut_t cut, auto split) {
    debug_assert(split.dfgs.size() == 2);
    auto& single_activity_dfg{split.dfgs[1]};
    auto& remainder_dfg{split.dfgs[0]};
    process_tree pt{};
    stop_token.stop_execution_if_requested();
    auto sub_eventlogs{max_par_cut{}.apply_split(miner_config, dfg, split.cut, context, stop_token)};
    debug_assert(sub_eventlogs.size() == 2);
    auto& single_activity_log{sub_eventlogs[1]};
    auto& remainder_log{sub_eventlogs[0]};
    miner_config.eventlog() = std::move(remainder_log);
    if (empty_traces_base_case::is_applicable(remainder_dfg)) {
      const auto counts{empty_traces_base_case::update_dfg(remainder_dfg)};
      miner_statistics.insert_or_increment(inductive_miner_statistics::empty_traces_base_case_count_key());
      stop_token.stop_execution_if_requested();
      pt = apply_cut_composite<IMPL>(cut, miner_config, remainder_dfg, context, miner_statistics, stop_token);
      pt = {process_tree::exclusive{{std::vector{{process_tree::tau{counts.empty_count}}, std::move(pt)}},
                                    {counts.empty_count, counts.nonempty_count}}};
    } else {
      stop_token.stop_execution_if_requested();
      pt = apply_cut_composite<IMPL>(cut, miner_config, remainder_dfg, context, miner_statistics, stop_token);
    }
    miner_config.eventlog() = std::move(single_activity_log);
    return process_tree{
        {process_tree::parallel{{{std::move(pt), inductive_miner_recurse(miner_config, single_activity_dfg, context,
                                                                         miner_statistics, stop_token)}},
                                dfg[boost::graph_bundle].log.trace_count}}};
  }};

  for (auto cut_impl : cut_implementations) {
    auto remainder_process_tree{std::visit(
        [&]<typename IMPL>(IMPL impl) -> std::optional<process_tree> {
          for (auto& split : parallel_dfgs) {
            if (auto cut{find_cut<IMPL>(split.dfgs.front())}; cut.first > 1) {
              miner_statistics.insert_or_increment(key<IMPL>);
              return compute_result(impl, cut, std::move(split));
            }
          }
          return std::nullopt;
        },
        cut_impl)};
    if (remainder_process_tree.has_value()) {
      return remainder_process_tree;
    }
  }

  return std::nullopt;
}

}  // namespace celonis::accelerator::operators::process
