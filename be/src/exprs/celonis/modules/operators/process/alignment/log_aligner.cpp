#include "log_aligner.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>

#include <boost/tuple/tuple.hpp>
#include <tbb/combinable.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/memory/batched_tracking_memory_resource.h"
#include "modules/common/timer.h"
#include "modules/operators/process/alignment/fallback/fallback_aligner.h"
#include "modules/operators/process/alignment/fitting_prefix/fitting_prefix_aligner.h"
#include "modules/operators/process/alignment/optimal/align_synchronous_product.h"
#include "modules/operators/process/petri_net/a_star/inconsistent_path_construction.h"
#include "modules/operators/process/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/petri_net/a_star/petri_net_wrapper.h"
#include "modules/operators/process/petri_net/a_star/shortest_path_heuristic.h"
#include "modules/operators/process/petri_net/murata/murata_reductions.h"
#include "modules/operators/process/petri_net/unfolding/unfolding_computation.h"
#include "modules/operators/process/alignment/rl_align/relaxation_labeling.h"
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"
#include "modules/operators/process/alignment/rl_align/rl_aligner.h"
#include "modules/operators/process/alignment/rl_align/rl_statistics/optimal_alignment_set_cover.h"

namespace celonis::accelerator::operators::process::alignment {

namespace {

template <typename T>
struct is_thread_local_container : std::false_type {};

template <typename T, typename ALLOCATOR, tbb::ets_key_usage_type ETS_KEY_TYPE>
struct is_thread_local_container<tbb::enumerable_thread_specific<T, ALLOCATOR, ETS_KEY_TYPE>> : std::true_type {};

template <typename... Ts>
struct is_thread_local_container<tbb::combinable<Ts...>> : std::true_type {};

template <class T>
inline constexpr bool is_thread_local_container_v = is_thread_local_container<T>::value;

template <typename T>
concept thread_local_container = is_thread_local_container_v<T>;

template <typename F, thread_local_container... THREAD_LOCALS>
auto bind_thread_locals_back(F&& f, const common::execution_context& context, THREAD_LOCALS&... thread_locals) {
  return [f = std::make_tuple(std::forward<F>(f)), &context,
          &thread_locals...]<typename RANGE>(RANGE&& range) -> decltype(auto) {
    return std::invoke(std::get<0>(f), context, std::forward<RANGE>(range), thread_locals.local()...);
  };
}

struct align_range {
  align_range(const log_aligner& aligner, std::span<trace_alignment_t> pruned_alignments,
              const log_aligner::dict_mapping_type& pruned_dict_mapping,
              const memory::cache::variant_trace_cache::traces_accessor_t& pruned_traces,
              const memory::cache::variant_trace_cache::trace_lengths_accessor_t& pruned_trace_lengths)
      : aligner_{aligner},
        pruned_alignments_{pruned_alignments},
        pruned_dict_mapping_{pruned_dict_mapping},
        pruned_traces_{pruned_traces},
        pruned_trace_lengths_{pruned_trace_lengths} {}

  template <typename RANGE>
  void operator()(const common::execution_context& context, const RANGE& range,
                  log_aligner::alignment_counters& counter, const fallback::fallback_aligner& fallback,
                  rl_align::rl_aligner& rl_aligner) const {
    const auto impl{[&, &aligner = this->aligner_, begin_range = std::begin(range),
                     end_range = std::end(range)](const auto& mapping) {
      for (row_id i{begin_range}; i < end_range; ++i) {
        const auto* pruned_trace{pruned_traces_[i]};
        const auto pruned_trace_length{pruned_trace_lengths_[i]};

        // Have to remap pruned traces to have the same activity IDs as in the full traces
        std::vector<row_id> pruned_mapped_trace(pruned_trace_length);
        std::ranges::transform(std::span{pruned_trace, pruned_trace_length}, begin(pruned_mapped_trace), mapping);

        auto [alignment, trace_counters] = aligner.align_variant(pruned_mapped_trace, fallback, rl_aligner, context);
        if (alignment) {
          trace_counters.alignment_cost = alignment.value().cost();
          trace_counters.successfully_computed_alignments = 1;
        } else {
          trace_counters.alignment_cost = 0;
          trace_counters.successfully_computed_alignments = 0;
        }
        pruned_alignments_[i] = alignment;
        counter += trace_counters;
      }
    }};
    if (pruned_dict_mapping_.has_value()) {
      impl([&map = pruned_dict_mapping_.value()](auto idx) { return map[idx]; });
    } else {
      impl(std::identity{});
    }
  }  // namespace celonis::accelerator::operators::process::alignment

 private:
  const log_aligner& aligner_;
  std::span<trace_alignment_t> pruned_alignments_;
  log_aligner::dict_mapping_type pruned_dict_mapping_;
  const memory::cache::variant_trace_cache::traces_accessor_t& pruned_traces_;
  const memory::cache::variant_trace_cache::trace_lengths_accessor_t& pruned_trace_lengths_;
};

auto get_range_aligner(const log_aligner& aligner, std::span<trace_alignment_t> pruned_alignments,
                       const log_aligner::dict_mapping_type& pruned_dict_mapping,
                       tbb::enumerable_thread_specific<log_aligner::alignment_counters>& counters,
                       const memory::cache::variant_trace_cache::traces_accessor_t& pruned_traces,
                       const memory::cache::variant_trace_cache::trace_lengths_accessor_t& pruned_trace_lengths,
                       tbb::enumerable_thread_specific<fallback::fallback_aligner>& fallback_aligners,
                       tbb::enumerable_thread_specific<rl_align::rl_aligner>& rl_aligners,
                       const common::execution_context& context) {
  return bind_thread_locals_back(
      align_range{aligner, pruned_alignments, pruned_dict_mapping, pruned_traces, pruned_trace_lengths}, context,
      counters, fallback_aligners, rl_aligners);
}

petri_net::unfolding_representation compute_unfolding(const petri_net::petri_net_representation& pn_repr,
                                                      const std::string& operator_name,
                                                      const common::execution_context& context) {
  input_output_mapper pn_io_mapper{};
  const petri_net::safe_petri_net_data pn_data{pn_io_mapper, pn_repr, operator_name};
  petri_net::petri_net_accessor pn_accessor{pn_data, context};
  petri_net::unfolding::unfolding_computation unf_computation{pn_accessor};
  auto unf_repr{unf_computation.compute_unfolding()};

  return unf_repr;
}

}  // namespace

log_aligner_config log_aligner_config::make_default() {
  log_aligner_config ret{};
  ret.num_iterations_fitting_prefix = 1'000;
  ret.max_number_trailing_model_moves = 100;
  ret.num_iterations_shortest_trace = 1'000'000;
  ret.num_a_star_iterations = 5'000;
  ret.num_analysis_runs = std::numeric_limits<row_id>::max();
  ret.num_constraints_kept = 8;  // we solve 8 constraints in parallel, so use a multiple of 8
  ret.rl_align_cfg = rl_align::make_small_rl_align_config();
  return ret;
}

std::pair<vector_of_alignments, log_aligner::agg_alignment_counters> log_aligner::execute(
    common::execution_context& context, const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
    std::string_view operator_name) {
  vector_of_alignments pruned_alignments(
      trace_alignment_allocator_type{std::make_shared<legacy_embedded_ctl::batched_tracking_memory_resource>()});
  const row_id number_of_pruned_variants{pruned_variant_trace_cache->get_num_traces()};
  pruned_alignments.resize(number_of_pruned_variants);
  tbb::enumerable_thread_specific<log_aligner::alignment_counters> counters{};

  const auto pruned_traces{pruned_variant_trace_cache->get_traces(context)};
  const auto pruned_trace_lengths{pruned_variant_trace_cache->get_trace_lengths(context)};

  // Compute fallback aligner: Basically the shortest Petri net run, and a method to align a sequence to it
  petri_net::petri_net_accessor pn_accessor_tt{pn_data_tt_, context};
  fallback::fallback_aligner fallback{
      pn_accessor_tt, pn_information_, config_.num_iterations_shortest_trace, config_.num_a_star_iterations,
      operator_name,  context};

  tbb::enumerable_thread_specific<fallback::fallback_aligner> fallback_aligners{fallback};
  tbb::enumerable_thread_specific<rl_align::rl_aligner> rl_aligners{
      [&pn_data_tt = std::as_const(pn_data_tt_), &pn_data_tf = std::as_const(pn_data_tf_),
       &pn_info = std::as_const(pn_information_), config = config_, &execution_ctx = std::as_const(context)]() {
        return rl_align::rl_aligner{pn_data_tt,
                                    pn_data_tf,
                                    pn_info,
                                    legacy_embedded_ctl::cast<row_id>(config.num_analysis_runs),
                                    legacy_embedded_ctl::cast<row_id>(config.num_constraints_kept),
                                    config.rl_align_cfg,
                                    execution_ctx};
      }};

  const auto align_range{get_range_aligner(*this, pruned_alignments, pruned_dict_mapping_, counters, pruned_traces,
                                           pruned_trace_lengths, fallback_aligners, rl_aligners, context)};
  // TODO(goulart.e) Check if different grain sizes change performance
  tbb::parallel_for(tbb::blocked_range<row_id>{0, number_of_pruned_variants}, align_range);

  // Collect the final statistics
  auto agg_counters{std::accumulate(std::cbegin(counters), std::cend(counters), alignment_counters{})};
  rl_align::rl_statistics::top_constraints final_statistics{};
  std::ranges::for_each(
      rl_aligners, [&final_statistics](const auto& s) { return final_statistics.update(s.statistics_aggregator()); });

  auto best_constraints{final_statistics.get_all_best()};
  static constexpr size_t MAX_CONSTRAINT_SETS_TO_LOG{5};
  if (size(best_constraints.data) > MAX_CONSTRAINT_SETS_TO_LOG) {
    best_constraints.data.resize(MAX_CONSTRAINT_SETS_TO_LOG);
  }

  return {pruned_alignments, {agg_counters, best_constraints}};
}

std::pair<trace_alignment_t, log_aligner::alignment_counters> log_aligner::align_variant(
    std::span<const row_id> pruned_mapped_trace, const fallback::fallback_aligner& fallback,
    rl_align::rl_aligner& rl_aligner, const common::execution_context& context) const {
  petri_net::petri_net_accessor pn_accessor_tt{pn_data_tt_, context};
  alignment_counters trace_counters{};

  // We get the fallback
  const auto fallback_alignment{fallback(pruned_mapped_trace, context)};

  // For empty variants, the alignment is the fallback
  if (pruned_mapped_trace.empty()) {
    return {fallback_alignment, trace_counters};
  }

  const auto prefix_alignment{fitting_prefix::fitting_prefix_aligner{config_.max_number_trailing_model_moves,
                                                                     config_.num_iterations_fitting_prefix,
                                                                     context}(pn_accessor_tt, pruned_mapped_trace)};
  if (prefix_alignment) {
    return {prefix_alignment.value(), trace_counters};
  }

  // try A*-search on the synchronous product for a few iterations
  common::timer timer_optimal{};
  auto optimal_alignment{optimal::search_synchronous_product_for_optimal_alignment(
      pn_accessor_tt, pruned_mapped_trace, config_.num_a_star_iterations, context)};
  timer_optimal.stop();
  trace_counters.time_optimal = timer_optimal.duration_us().count();

  if (optimal_alignment.has_value()) {
    trace_counters.pruned_variants_computed_optimal = 1;
    return {optimal_alignment.value(), trace_counters};
  }

  // If everything fails, we approximate. The fallback (if it exists) gives an upper bound on the alignment cost
  const int fallback_cost{fallback_alignment.has_value() ? legacy_embedded_ctl::cast<int>(fallback_alignment.value().cost())
                                                         : std::numeric_limits<int>::max()};

  trace_counters.pruned_variants_computed_relaxation_labeling = 1;
  common::timer timer_relaxation_labeling{};
  auto [alignment, optimizations_solved]{rl_aligner(pruned_mapped_trace, fallback_cost, context)};
  timer_relaxation_labeling.stop();
  trace_counters.time_relaxation_labeling = timer_relaxation_labeling.duration_us().count();
  trace_counters.optimizations_solved = optimizations_solved;
  trace_counters.successful_relaxation_labelings = alignment.has_value() ? 1 : 0;

  if (!alignment.has_value() || legacy_embedded_ctl::cast<int>(alignment.value().cost()) > fallback_cost) {
    alignment = fallback_alignment;
  }

  return {alignment, trace_counters};
}

log_aligner::alignment_counters log_aligner::alignment_counters::operator+(
    const log_aligner::alignment_counters& rhs) const noexcept {
  alignment_counters ret{*this};
  ret += rhs;
  return ret;
}

log_aligner::alignment_counters& log_aligner::alignment_counters::operator+=(
    const log_aligner::alignment_counters& rhs) noexcept {
  pruned_variants_computed_optimal += rhs.pruned_variants_computed_optimal;
  pruned_variants_computed_relaxation_labeling += rhs.pruned_variants_computed_relaxation_labeling;
  optimizations_solved += rhs.optimizations_solved;
  successful_relaxation_labelings += rhs.successful_relaxation_labelings;
  alignment_cost += rhs.alignment_cost;
  successfully_computed_alignments += rhs.successfully_computed_alignments;
  time_optimal += rhs.time_optimal;
  time_relaxation_labeling += rhs.time_relaxation_labeling;
  return *this;
}

petri_net::unfolding_representation unfold(const petri_net::petri_net_representation& pn_repr,
                                           const std::unordered_set<std::string>& keep_transitions,
                                           alignment_statistics& stats, common::execution_context& operator_context,
                                           const std::string& user_facing_name) {
  const auto [visible_labels_count, distinct_visible_labels_count]{pn_repr.count_labels()};
  stats.petri_net_visible_label_count = visible_labels_count;
  stats.distinct_petri_net_visible_label_count = distinct_visible_labels_count;

  const auto reduced_pn_repr{petri_net::murata::reduce_murata(pn_repr, keep_transitions)};

  common::timer unfolding_timer{};
  auto unf_repr{compute_unfolding(reduced_pn_repr, user_facing_name, operator_context)};
  unfolding_timer.stop();
  stats.time_unfolding = unfolding_timer.duration_us().count();
  return unf_repr;
}

log_aligner compute_aligner(const petri_net::unfolding_representation& unf_repr,
                            log_aligner::dict_mapping_type pruned_dict_mapping,
                            const log_aligner_config& log_aligner_cfg, input_output_mapper& io_mapper,
                            alignment_statistics& stats, common::execution_context& operator_context,
                            const std::string& user_facing_name) {
  common::timer preprocess_timer{};
  petri_net::safe_petri_net_data pn_data_tt{io_mapper, reconnect_unfolding(unf_repr, {true, true}), user_facing_name};
  petri_net::safe_petri_net_data pn_data_tf{io_mapper, reconnect_unfolding(unf_repr, {true, false}), user_facing_name};
  preprocess_timer.stop();
  stats.time_preprocess = preprocess_timer.duration_us().count();

  if (!unf_repr.petri_net_repr.transitions.empty() &&
      (pn_data_tt.final_markings.empty() || pn_data_tf.final_markings.empty())) {
    throw common::cpm_exception{"{}: The start and end places in the Petri net seem to be disconnected.",
                                user_facing_name};
  }

  return log_aligner{std::move(pn_data_tt), std::move(pn_data_tf), pruned_dict_mapping, log_aligner_cfg,
                     operator_context};
}

vector_of_alignments execute_aligner(log_aligner& aligner, alignment_statistics& stats,
                                     const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
                                     common::execution_context& operator_context, const std::string& user_facing_name) {
  const auto [pruned_alignments,
              perf_counters]{aligner.execute(operator_context, pruned_variant_trace_cache, user_facing_name)};

  stats.pruned_variants_computed_optimal += perf_counters.pruned_variants_computed_optimal;
  stats.pruned_variants_computed_relaxation_labeling += perf_counters.pruned_variants_computed_relaxation_labeling;
  stats.optimizations_solved += perf_counters.optimizations_solved;
  stats.successful_relaxation_labelings += perf_counters.successful_relaxation_labelings;
  stats.total_cost_pruned_variants += perf_counters.alignment_cost;
  stats.successfully_computed_pruned_variants += perf_counters.successfully_computed_alignments;
  stats.time_optimal += perf_counters.time_optimal;
  stats.time_relaxation_labeling += perf_counters.time_relaxation_labeling;
  stats.pruned_variants_count = pruned_alignments.size();

  std::ranges::copy(perf_counters.best_compatibilities.data, std::back_inserter(stats.constraints));

  return pruned_alignments;
}

// Build the mapping event_int_id => transition_str_id
transition_id_to_str_mapping_t compute_mapping(const petri_net::unfolding_representation& unf_repr,
                                               const input_output_mapper& io_mapper) {
  transition_id_to_str_mapping_t mapping{};
  for (const auto& [event_str_id, _] : unf_repr.petri_net_repr.transitions) {
    const auto event_id{io_mapper.get_transition_for_str_id(event_str_id)};
    const auto transition_str_id{unf_repr.event_id_to_original_node_id.at(event_str_id)};
    mapping.emplace(event_id, transition_str_id);
  }
  return mapping;
}

log_aligner_wrapper log_aligner_wrapper::create(const petri_net::petri_net_representation& pn_repr,
                                                const std::unordered_set<std::string>& keep_transitions,
                                                log_aligner::dict_mapping_type pruned_dict_mapping,
                                                const log_aligner_config& log_aligner_cfg,
                                                common::execution_context& operator_context,
                                                const std::string& user_facing_name) {
  input_output_mapper io_mapper{};
  alignment_statistics stats{};
  auto unf_repr{unfold(pn_repr, keep_transitions, stats, operator_context, user_facing_name)};
  auto aligner{compute_aligner(unf_repr, pruned_dict_mapping, log_aligner_cfg, io_mapper, stats, operator_context,
                               user_facing_name)};
  return log_aligner_wrapper{std::move(io_mapper), std::move(stats), unf_repr, std::move(aligner)};
}

std::tuple<vector_of_alignments, transition_id_to_str_mapping_t> log_aligner_wrapper::operator()(
    const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache, common::execution_context& operator_context,
    const std::string& user_facing_name) {
  return {execute_aligner(aligner_, stats_, pruned_variant_trace_cache, operator_context, user_facing_name),
          compute_mapping(unf_repr_, io_mapper_)};
}

}  // namespace celonis::accelerator::operators::process::alignment
