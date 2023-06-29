#include "inductive_miner.h"

#include <optional>

#include <fmt/format.h>

#include "ctl/assert.h"
#ifdef CELOSTAR
#include "inductive_miner/base_case_strategy.h"
#include "inductive_miner/cut_strategy.h"
#include "inductive_miner/fallback_strategy.h"
#include "inductive_miner/process_tree_reduction.h"
#include "modules/common/execution_context.h"  // To be deleted
#else
#include "ctl/conversion.h"
#include "modules/memory/column.h"
#include "modules/operators/process/dot_format_helper.h"
#include "modules/operators/process/inductive_miner/base_case_strategy.h"
#include "modules/operators/process/inductive_miner/cut_strategy.h"
#include "modules/operators/process/inductive_miner/fallback_strategy.h"
#include "modules/operators/process/inductive_miner/process_tree_reduction.h"
#endif

namespace celonis::accelerator::operators::process {

namespace {

template <typename T>
struct composite_impl {
  template <typename... PARAMETERS>
  static process_tree apply_base_cut(PARAMETERS&&... parameters) {
    return apply_cut_composite<T>(std::forward<PARAMETERS>(parameters)...);
  }

  template <typename... PARAMETERS>
  static process_tree apply_trivial_cut(PARAMETERS&&... parameters) {
    return T::apply(std::forward<PARAMETERS>(parameters)...);
  }
};

template <template <typename> class APPLY_CUT>
auto inductive_miner_impl(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                          common::execution_context& context, inductive_miner_statistics& miner_statistics) {
  // #lizard forgives

  // Test for trivial cases.
  if (empty_log_base_case::is_applicable(dfg)) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::empty_log_base_case_count_key());
    return APPLY_CUT<empty_log_base_case>::apply_trivial_cut();
  }

  if (empty_traces_base_case::is_applicable(dfg)) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::empty_traces_base_case_count_key());
    return APPLY_CUT<empty_traces_base_case>::apply_trivial_cut(miner_config, dfg, context, miner_statistics);
  }

  if (noisy_single_activity_base_case::is_applicable(dfg, miner_config.filter_config)) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_single_activity_base_case_count_key());
    return APPLY_CUT<noisy_single_activity_base_case>::apply_trivial_cut(dfg);
  }

  if (single_activity_base_case::is_applicable(dfg)) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::single_activity_base_case_count_key());
    return APPLY_CUT<single_activity_base_case>::apply_trivial_cut(dfg);
  }

  // Test for cuts to apply.
  if (const auto& xor_cut{find_cut<max_xor_cut>(dfg)}; xor_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::xor_count_key());
    return APPLY_CUT<max_xor_cut>::apply_base_cut(xor_cut, miner_config, dfg, context, miner_statistics);
  }

  if (const auto& sequence_cut{find_cut<max_seq_cut>(dfg)}; sequence_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::seq_count_key());
    return APPLY_CUT<max_seq_cut>::apply_base_cut(sequence_cut, miner_config, dfg, context, miner_statistics);
  }

  if (const auto& parallel_cut{find_cut<max_par_cut>(dfg)}; parallel_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::par_count_key());
    return APPLY_CUT<max_par_cut>::apply_base_cut(parallel_cut, miner_config, dfg, context, miner_statistics);
  }

  if (const auto& redo_cut{find_cut<max_redo_cut>(dfg)}; redo_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::loop_count_key());
    return APPLY_CUT<max_redo_cut>::apply_base_cut(redo_cut, miner_config, dfg, context, miner_statistics);
  }

  // Filter the directly-follows graph.
  if (miner_config.filter_config.vertices) {
    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].start_vertices, miner_config.filter_config);
    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].end_vertices, miner_config.filter_config);
  }

  directly_follows_graph filtered_dfg{dfg};
  if (miner_config.filter_config.edges) {
    filtered_dfg = dfg::filter_dfg_edges(dfg, miner_config.filter_config);
  }

  // Iff something changed, test for cuts to apply.
  // TODO (goulart.e) filter on both thresholds (see CPL-6752)
  if (miner_config.filter_config.vertices || miner_config.filter_config.edges) {
    if (const auto& xor_cut{find_cut<noisy_xor_cut>(filtered_dfg)}; xor_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_xor_count_key());
      return APPLY_CUT<noisy_xor_cut>::apply_base_cut(xor_cut, miner_config, filtered_dfg, context, miner_statistics);
    }

    if (const auto& sequence_cut{find_cut<max_seq_cut>(filtered_dfg)}; sequence_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_seq_count_key());
      return APPLY_CUT<max_seq_cut>::apply_base_cut(sequence_cut, miner_config, filtered_dfg, context,
                                                    miner_statistics);
    }

    if (const auto& parallel_cut{find_cut<max_par_cut>(filtered_dfg)}; parallel_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_par_count_key());
      return APPLY_CUT<max_par_cut>::apply_base_cut(parallel_cut, miner_config, filtered_dfg, context,
                                                    miner_statistics);
    }

    if (const auto& redo_cut{find_cut<max_redo_cut>(filtered_dfg)}; redo_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_loop_count_key());
      return APPLY_CUT<max_redo_cut>::apply_base_cut(redo_cut, miner_config, filtered_dfg, context, miner_statistics);
    }
  }

  // Try specific fallback strategies.
  if (const auto& activity_once_per_trace_cut{find_cut<activity_once_per_trace>(miner_config, dfg, context)};
      activity_once_per_trace_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::activity_once_per_trace_count_key());
    return APPLY_CUT<activity_once_per_trace>::apply_base_cut(activity_once_per_trace_cut, miner_config, dfg, context,
                                                              miner_statistics);
  }

  if (auto activity_concurrent_result{
          activity_concurrent::apply_if_applicable(miner_config, filtered_dfg, context, miner_statistics)}) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::activity_concurrent_count_key());
    return activity_concurrent_result.value();
  }

  if (auto split_first_activities{strict_tau_loop_fallback::is_applicable(miner_config, filtered_dfg, context)}) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::strict_tau_loop_count_key());
    return APPLY_CUT<strict_tau_loop_fallback>::apply_base_cut(miner_config, filtered_dfg, context, miner_statistics);
  }

  if (auto split_first_activities{slack_tau_loop_fallback::is_applicable(miner_config, filtered_dfg, context)}) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::slack_tau_loop_count_key());
    return APPLY_CUT<slack_tau_loop_fallback>::apply_base_cut(miner_config, filtered_dfg, context, miner_statistics);
  }

  miner_statistics.insert_or_increment(inductive_miner_statistics::flower_fallback_count_key());
  return APPLY_CUT<flower_fallback>::apply_trivial_cut(filtered_dfg);
}

}  // namespace

process_tree inductive_miner_recurse(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                     common::execution_context& context, inductive_miner_statistics& miner_statistics) {
  return inductive_miner_impl<composite_impl>(miner_config, dfg, context, miner_statistics);
}

inductive_miner_result inductive_miner(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                       common::execution_context& parent_context,
                                       inductive_miner_statistics& miner_statistics, const cel_string_t* dict) {
  auto context{parent_context.create_sub_context("inductive_miner", {})};
  auto result{inductive_miner_recurse(miner_config, dfg, context, miner_statistics)};
  auto is_valid{is_valid_tree(result)};
  warning_assert(is_valid,
#ifdef CELOSTAR
                 // TODO(j.kim): Add pt2dot.
                 "The inductive miner generates an inconsistent process tree");
#else
                 fmt::format("The inductive miner generates an inconsistent process tree {}", pt2dot(result, nullptr)));
#endif
  reduction::reduce_to_normal_form(result, dict);
  if (!is_valid_tree(result)) {
    is_valid = false;
    warning_assert(
#ifdef CELOSTAR
        false, "The reduction rules generate an inconsistent process tree");
#else
        false, fmt::format("The reduction rules generate an inconsistent process tree {}", pt2dot(result, nullptr)));
#endif
  }
  return {result, is_valid};
}

}  // namespace celonis::accelerator::operators::process
