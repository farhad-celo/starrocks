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
#include "ctl/exception_traits.h"
#include "log/log.h"
#include "modules/memory/column.h"
#include "modules/operators/process/dot_format_helper.h"
#include "modules/operators/process/inductive_miner/base_case_strategy.h"
#include "modules/operators/process/inductive_miner/cut_strategy.h"
#include "modules/operators/process/inductive_miner/fallback_strategy.h"
#include "modules/operators/process/inductive_miner/process_tree_reduction.h"
#include "modules/operators/process/inductive_miner/replay_eventlog_on_process_tree.h"
#include "modules/operators/process/mka/m2_abstraction.h"
#include "modules/operators/process/mka/mk_abstraction.h"
#include "modules/operators/process/mka/mka_fitness.h"
#include "modules/operators/process/mka/mka_precision.h"
#endif

namespace celonis::accelerator::operators::process {
static constexpr row_id METRICS_DECIMAL_DIGITS_FACTOR{10000};
static constexpr row_id K_PRECISION{2};
static constexpr std::chrono::milliseconds MKA_METRICS_TIMEOUT{2000};

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
                          const common::execution_context& context, inductive_miner_statistics& miner_statistics,
                          const cube::execution::tracking::stop_token& stop_token) {
  // #lizard forgives

  // Test for trivial cases.
  if (empty_log_base_case::is_applicable(dfg)) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::empty_log_base_case_count_key());
    return APPLY_CUT<empty_log_base_case>::apply_trivial_cut();
  }

  if (empty_traces_base_case::is_applicable(dfg)) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::empty_traces_base_case_count_key());
    return APPLY_CUT<empty_traces_base_case>::apply_trivial_cut(miner_config, dfg, context, miner_statistics,
                                                                stop_token);
  }

  if (noisy_single_activity_base_case::is_applicable(dfg, miner_config.filter_config())) {
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
    return APPLY_CUT<max_xor_cut>::apply_base_cut(xor_cut, miner_config, dfg, context, miner_statistics, stop_token);
  }

  if (const auto& sequence_cut{find_cut<max_seq_cut>(dfg)}; sequence_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::seq_count_key());
    return APPLY_CUT<max_seq_cut>::apply_base_cut(sequence_cut, miner_config, dfg, context, miner_statistics,
                                                  stop_token);
  }

  if (const auto& parallel_cut{find_cut<max_par_cut>(dfg)}; parallel_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::par_count_key());
    return APPLY_CUT<max_par_cut>::apply_base_cut(parallel_cut, miner_config, dfg, context, miner_statistics,
                                                  stop_token);
  }

  if (const auto& redo_cut{find_cut<max_redo_cut>(dfg)}; redo_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::loop_count_key());
    return APPLY_CUT<max_redo_cut>::apply_base_cut(redo_cut, miner_config, dfg, context, miner_statistics, stop_token);
  }

  // Filter the directly-follows graph.
  if (miner_config.filter_config().vertices) {
    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].start_vertices, miner_config.filter_config());
    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].end_vertices, miner_config.filter_config());
  }

  directly_follows_graph filtered_dfg{dfg};
  if (miner_config.filter_config().edges) {
    filtered_dfg = dfg::filter_dfg_edges(dfg, miner_config.filter_config());
  }

  // Iff something changed, test for cuts to apply.
  // TODO (goulart.e) filter on both thresholds (see CPL-6752)
  if (miner_config.filter_config().vertices || miner_config.filter_config().edges) {
    if (const auto& xor_cut{find_cut<noisy_xor_cut>(filtered_dfg)}; xor_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_xor_count_key());
      return APPLY_CUT<noisy_xor_cut>::apply_base_cut(xor_cut, miner_config, filtered_dfg, context, miner_statistics,
                                                      stop_token);
    }

    if (const auto& sequence_cut{find_cut<max_seq_cut>(filtered_dfg)}; sequence_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_seq_count_key());
      return APPLY_CUT<max_seq_cut>::apply_base_cut(sequence_cut, miner_config, filtered_dfg, context, miner_statistics,
                                                    stop_token);
    }

    if (const auto& parallel_cut{find_cut<max_par_cut>(filtered_dfg)}; parallel_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_par_count_key());
      return APPLY_CUT<max_par_cut>::apply_base_cut(parallel_cut, miner_config, filtered_dfg, context, miner_statistics,
                                                    stop_token);
    }

    if (const auto& redo_cut{find_cut<max_redo_cut>(filtered_dfg)}; redo_cut.first > 1) {
      miner_statistics.insert_or_increment(inductive_miner_statistics::noisy_loop_count_key());
      return APPLY_CUT<max_redo_cut>::apply_base_cut(redo_cut, miner_config, filtered_dfg, context, miner_statistics,
                                                     stop_token);
    }
  }

  // Try specific fallback strategies.
  if (const auto& activity_once_per_trace_cut{
          find_cut<activity_once_per_trace>(miner_config, dfg, context, stop_token)};
      activity_once_per_trace_cut.first > 1) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::activity_once_per_trace_count_key());
    return APPLY_CUT<activity_once_per_trace>::apply_base_cut(activity_once_per_trace_cut, miner_config, dfg, context,
                                                              miner_statistics, stop_token);
  }

  if (auto activity_concurrent_result{activity_concurrent::apply_if_applicable(miner_config, filtered_dfg, context,
                                                                               miner_statistics, stop_token)}) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::activity_concurrent_count_key());
    return activity_concurrent_result.value();
  }

  if (auto split_first_activities{strict_tau_loop_fallback::is_applicable(miner_config, filtered_dfg, context)}) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::strict_tau_loop_count_key());
    return APPLY_CUT<strict_tau_loop_fallback>::apply_base_cut(miner_config, filtered_dfg, context, miner_statistics,
                                                               stop_token);
  }

  if (auto split_first_activities{slack_tau_loop_fallback::is_applicable(miner_config, filtered_dfg, context)}) {
    miner_statistics.insert_or_increment(inductive_miner_statistics::slack_tau_loop_count_key());
    return APPLY_CUT<slack_tau_loop_fallback>::apply_base_cut(miner_config, filtered_dfg, context, miner_statistics,
                                                              stop_token);
  }

  miner_statistics.insert_or_increment(inductive_miner_statistics::flower_fallback_count_key());
  return APPLY_CUT<flower_fallback>::apply_trivial_cut(filtered_dfg);
}

#ifndef CELOSTAR
mka::mk_abstraction get_log_m2a(const inductive_miner_config& operator_config, std::stop_token stop_token) {
  const auto& operator_context{operator_config.execution_context()};
  return std::visit(
      ctl::overloaded(
          [&operator_context,
           &stop_token](const splittable_eventlog_config_for_using_entire_eventlog& eventlog_config) {
            auto filter{eventlog_config.selections};
            filter.flip();
            return mka::mka_from_eventlog(K_PRECISION, eventlog_config.activities, eventlog_config.cases, filter,
                                          eventlog_config.grain_size, stop_token, operator_context);
          },
          [&operator_context, &stop_token](const splittable_eventlog_config_for_using_variants& variant_config) {
            return mka::mka_from_variant_trace_cache(K_PRECISION, *variant_config.variant_trace_cache_ptr,
                                                     stop_token,  // NOLINT(performance-unnecessary-value-param)
                                                     operator_context);
          }),
      operator_config.eventlog_config());
}

format::json::json_object_t log_metrics(std::stop_token stop_token,  // NOLINT(performance-unnecessary-value-param)
                                        const process_tree& tree, const inductive_miner_config& operator_config,
                                        inductive_miner_statistics& stats, const common::execution_context& context) {
  mka::mk_abstraction mka_log{get_log_m2a(operator_config, stop_token)};  // NOLINT(performance-unnecessary-value-param)
  mka::mk_abstraction mka_model{mka::m2_abstraction::from_process_tree(
      tree, stop_token, context)};  // NOLINT(performance-unnecessary-value-param)

  if (stop_token.stop_requested()) {
    return format::json::json_object_t{};
  }

  const auto precision{mka::compute_precision(mka_model, mka_log)};
  const auto fitness{mka::compute_fitness(mka_model, mka_log)};
  // As statistics are tracked as size_t we multiply to capture 4 decimal digits
  const auto precision_conv{precision * METRICS_DECIMAL_DIGITS_FACTOR};
  const auto fitness_conv{fitness * METRICS_DECIMAL_DIGITS_FACTOR};
  stats.insert_or_assign(inductive_miner_statistics::precision_key(), static_cast<size_t>(precision_conv));
  stats.insert_or_assign(inductive_miner_statistics::fitness_key(), static_cast<size_t>(fitness_conv));

  format::json::json_object_t log_message{};
  log_message.emplace("Precision of model", precision);
  log_message.emplace("Fitness of model", fitness);
  log_message.emplace("Number of nodes mka model", mka_model.get_states().size());
  log_message.emplace("Number of edges mka model", mka_model.get_edges().size());
  log_message.emplace("Number of nodes mka log", mka_log.get_states().size());
  log_message.emplace("Number of edges mka log", mka_log.get_edges().size());
  return log_message;
}

void timeout_protected_metric_logging(const process_tree& tree, const inductive_miner_config& operator_config,
                                      inductive_miner_statistics& stats, const common::execution_context& context) {
  try {
    common::timer metrics_logging_timer{};
    std::packaged_task<format::json::json_object_t(std::stop_token,
                                                   const process_tree&,  // NOLINT(performance-unnecessary-value-param)
                                                   const inductive_miner_config&, inductive_miner_statistics&,
                                                   const common::execution_context&)>
        task(log_metrics);
    auto future{task.get_future()};

    std::jthread thr(std::move(task), std::ref(tree), std::ref(operator_config), std::ref(stats), std::ref(context));

    if (future.wait_for(MKA_METRICS_TIMEOUT) == std::future_status::ready) {
      auto log_message{future.get()};
      metrics_logging_timer.stop();
      log_message.emplace("MKA Duration", metrics_logging_timer.duration().count());
      log::jinfo("MKA Quality", log_message);
    } else {
      log::jinfo("MKA metrics calculation timed out.", {{"timeout_ms", MKA_METRICS_TIMEOUT.count()}});
    }

  } catch (const ctl::retryable_error& e) {
    log::jwarn("MKA process quality metrics logging failed due to a retryable exception.",
               {{"exception", e.internal_message()}});
  } catch (const std::exception& e) {
    log::jwarn("MKA process quality metrics logging failed due to an unhandled exception.", {{"exception", e.what()}});
  } catch (...) {
    log::jwarn("MKA process quality metrics logging failed due to an unknown reason");
  }
}
#endif

}  // namespace

process_tree inductive_miner_recurse(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                     const common::execution_context& context,
                                     inductive_miner_statistics& miner_statistics,
                                     const cube::execution::tracking::stop_token& stop_token) {
  return inductive_miner_impl<composite_impl>(miner_config, dfg, context, miner_statistics, stop_token);
}

inductive_miner_result inductive_miner(inductive_miner_config& miner_config, directly_follows_graph& dfg,
                                       inductive_miner_statistics& miner_statistics,
                                       const cube::execution::tracking::stop_token& stop_token,
                                       const cel_string_t* dict) {
  const auto context{miner_config.execution_context().create_sub_context("inductive_miner", {})};
  auto result{inductive_miner_recurse(miner_config, dfg, context, miner_statistics, stop_token)};
  auto is_valid{is_valid_tree(result)};
  warning_assert(is_valid,
#ifdef CELOSTAR
                 // TODO(j.kim): Add pt2dot.
                 "The inductive miner generates an inconsistent process tree");
#else
                 fmt::format("The inductive miner generates an inconsistent process tree {}", pt2dot(result, nullptr)));
#endif
  reduction::reduce_to_normal_form(result, stop_token, dict);
  if (!is_valid_tree(result)) {
    is_valid = false;
    warning_assert(
#ifdef CELOSTAR
        false, "The reduction rules generate an inconsistent process tree");
#else
        false, fmt::format("The reduction rules generate an inconsistent process tree {}", pt2dot(result, nullptr)));
#endif
  }

  stop_token.stop_execution_if_requested();
#ifndef CELOSTAR
  timeout_protected_metric_logging(result, miner_config, miner_statistics, context);

  // TODO(a.swoboda) also propagate the stop token into the replay
  // TODO(n.weber): In a follow up, the config probably can't be used to decide this. Think about a proper design then.
  if (miner_config.should_do_replay_to_fix_counts()) {
    common::runtime_assert(is_valid, "Invalid tree produced in variant based IM.");
    const auto& [activity_column, case_column]{miner_config.replay_input()};
    const cube::filter_bitset_t filter_use_all{
        ctl::cast_unsigned(activity_column->get_row_count(miner_config.execution_context())), true};
    result = replay_eventlog_on_process_tree(result, activity_column, case_column, filter_use_all,
                                             miner_config.execution_context());
    warning_assert(is_valid_tree(result),
                   "Invalid tree produced after replay");
                   fmt::format("Invalid tree produced after replay: {}", pt2dot(result, nullptr)));
  }
#endif

  return {result, is_valid};
}

}  // namespace celonis::accelerator::operators::process
