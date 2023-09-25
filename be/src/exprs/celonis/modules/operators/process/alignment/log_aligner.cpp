#include "log_aligner.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>

#include <boost/iterator/zip_iterator.hpp>
#include <boost/tuple/tuple.hpp>
#include <tbb/combinable.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "modules/common/timer.h"
#include "modules/operators/process/alignment/align_synchronous_product.h"
#include "modules/operators/process/alignment/compute_fitting_solution.h"
#include "modules/operators/process/alignment/petri_net/a_star/inconsistent_path_construction.h"
#include "modules/operators/process/alignment/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/alignment/petri_net/a_star/petri_net_wrapper.h"
#include "modules/operators/process/alignment/petri_net/a_star/shortest_path_heuristic.h"
#include "modules/operators/process/alignment/petri_net/a_star/synchronous_product.h"
#include "modules/operators/process/alignment/petri_net/a_star/synchronous_product_heuristic.h"
#include "modules/operators/process/alignment/petri_net/a_star/synchronous_product_path_construction.h"
#include "modules/operators/process/alignment/petri_net/murata/murata_reductions.h"
#include "modules/operators/process/alignment/petri_net/unfolding/unfolding_computation.h"
#include "modules/operators/process/alignment/relaxation_labeling.h"
#include "modules/operators/process/alignment/rl_statistics/optimal_alignment_set_cover.h"

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

template <typename T>
void extend_with(std::vector<T>& v, std::vector<T>&& w) {
  v.insert(end(v), std::make_move_iterator(begin(w)), std::make_move_iterator(end(w)));
}
template <typename T>
void extend_with(std::vector<T>& v, const std::vector<T>& w) {
  v.insert(end(v), begin(w), end(w));
}

struct align_range {
  align_range(const log_aligner& aligner, std::span<trace_alignment> pruned_alignments,
              const log_aligner::dict_mapping_type& pruned_dict_mapping,
              const memory::cache::variant_trace_cache::traces_accessor_t& pruned_traces,
              const memory::cache::variant_trace_cache::trace_lengths_accessor_t& pruned_trace_lengths,
              const constraints_config_set& constraint_set)
      : aligner_{aligner},
        pruned_alignments_{pruned_alignments},
        pruned_dict_mapping_{pruned_dict_mapping},
        pruned_traces_{pruned_traces},
        pruned_trace_lengths_{pruned_trace_lengths},
        constraint_set_{constraint_set} {}

  template <typename RANGE, typename... STATISTICS>
  void operator()(const common::execution_context& context, const RANGE& range,
                  log_aligner::alignment_counters& counter, gap_filler& filler,
                  STATISTICS&... enumerable_thread_specific_statistics) const {
    const std::vector<rl_statistics::statistics_base*> statistics{
        dynamic_cast<rl_statistics::statistics_base*>(std::addressof(enumerable_thread_specific_statistics))...};
    const auto impl{[&, &aligner = this->aligner_, begin_range = std::begin(range),
                     end_range = std::end(range)](const auto& mapping) {
      for (row_id i{begin_range}; i < end_range; ++i) {
        const auto* pruned_trace{pruned_traces_[i]};
        const auto pruned_trace_length{pruned_trace_lengths_[i]};

        // Have to remap pruned traces to have the same activity IDs as in the full traces
        std::vector<row_id> pruned_mapped_trace(pruned_trace_length);
        std::ranges::transform(std::span{pruned_trace, pruned_trace_length}, begin(pruned_mapped_trace), mapping);

        auto [alignment, trace_counters] =
            aligner.align_variant(pruned_mapped_trace, filler, constraint_set_, statistics, context);
        trace_counters.alignment_cost = alignment.cost();
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
  std::span<trace_alignment> pruned_alignments_;
  log_aligner::dict_mapping_type pruned_dict_mapping_;
  const memory::cache::variant_trace_cache::traces_accessor_t& pruned_traces_;
  const memory::cache::variant_trace_cache::trace_lengths_accessor_t& pruned_trace_lengths_;
  const constraints_config_set& constraint_set_;
};

template <typename... STATISTICS>
auto get_range_aligner(const log_aligner& aligner, std::span<trace_alignment> pruned_alignments,
                       const log_aligner::dict_mapping_type& pruned_dict_mapping,
                       tbb::enumerable_thread_specific<log_aligner::alignment_counters>& counters,
                       const memory::cache::variant_trace_cache::traces_accessor_t& pruned_traces,
                       const memory::cache::variant_trace_cache::trace_lengths_accessor_t& pruned_trace_lengths,
                       const constraints_config_set& constraint_set,
                       tbb::enumerable_thread_specific<gap_filler>& gap_fillers,
                       const common::execution_context& context, STATISTICS&... enumerable_thread_specific_statistics) {
  return bind_thread_locals_back(
      align_range{aligner, pruned_alignments, pruned_dict_mapping, pruned_traces, pruned_trace_lengths, constraint_set},
      context, counters, gap_fillers, enumerable_thread_specific_statistics...);
}

petri_net::unfolding_representation compute_unfolding(const petri_net::petri_net_representation& pn_repr,
                                                      const std::string& operator_name) {
  input_output_mapper pn_io_mapper{};
  const petri_net::safe_petri_net_data pn_data{pn_io_mapper, pn_repr, operator_name};
  petri_net::petri_net_accessor pn_accessor{pn_data};
  petri_net::unfolding::unfolding_computation unf_computation{std::move(pn_accessor)};
  auto unf_repr{unf_computation.compute_unfolding()};

  return unf_repr;
}

}  // namespace

std::optional<sequence_aligner::sequence_type> get_shortest_pn_trace(const petri_net::safe_petri_net_data& pn_data,
                                                                     const petri_net_information& pn_information,
                                                                     std::string_view operator_name,
                                                                     const common::execution_context& context) {
  petri_net::petri_net_accessor pn_accessor{pn_data};
  debug_assert(!pn_accessor.get_final_markings().empty());

  const auto baseline{petri_net::a_star::a_star_search(
      petri_net::a_star::petri_net_wrapper{pn_accessor},
      petri_net::a_star::heuristic_to_markings{pn_accessor.get_final_markings(), pn_information.shortest_paths_tt,
                                               pn_accessor},
      petri_net::a_star::inconsistent_path_construction<petri_net::marking_type, petri_net::petri_net_transition_id,
                                                        petri_net::a_star::heuristic_to_markings::cost_type,
                                                        boost::hash<petri_net::marking_type>>{context},
      pn_accessor.get_initial_marking(),
      std::numeric_limits<petri_net::a_star::heuristic_to_markings::cost_type>::max(), 1'000'000)};

  using nothing_found = petri_net::a_star::nothing_found;
  if (std::holds_alternative<nothing_found>(baseline)) {
    if (std::get<nothing_found>(baseline) == nothing_found::AT_ALL) {
      throw common::cpm_exception{"{}: The start and end places in the Petri net seem to be disconnected.",
                                  operator_name};
    }
    return std::nullopt;
  }

  using transitions_type = petri_net::a_star::petri_net_wrapper::transition_list_type;
  const auto& transitions{std::get<transitions_type>(baseline)};
  sequence_aligner::sequence_type shortest_run{};
  shortest_run.reserve(transitions.size());
  std::ranges::transform(transitions, std::back_inserter(shortest_run),
                         [&pn_accessor](const auto& t) { return std::make_pair<>(t, pn_accessor.get_label(t)); });
  return shortest_run;
}

// TODO (goulart.e) solver_config should already be passed to the multiple_rl_instances_solver
std::vector<petri_net::rl_problem_solution_t> rl_solutions_dense(const rl_problem_instance_builder& problem_builder,
                                                                 multiple_rl_problem_instances& rl_problems,
                                                                 const solver_config& solver_cfg,
                                                                 const constraints_config_set& constraints) {
  std::vector<petri_net::rl_problem_solution_t> result{};
  for (const auto& constraint : constraints.data) {
    if (!rl_problems.has_free_slot()) {
      rl_problems.assign_compatibilities();
      rl_problems.solve_problem_instance(solver_cfg);
      extend_with(result, problem_builder.extract_solutions(rl_problems));
      rl_problems.reset_slots();
    }
    rl_problems.add_compatibilities(constraint);
  }
  rl_problems.assign_compatibilities();
  rl_problems.solve_problem_instance(solver_cfg);
  extend_with(result, problem_builder.extract_solutions(rl_problems));
  return result;
}

using rl_sparse_solutions_type = std::unordered_map<constraints_config, petri_net::rl_problem_solution_t,
                                                    constraints_behavioral_hash, constraints_equal_to>;

rl_sparse_solutions_type rl_solutions_sparse(const rl_problem_instance_builder& problem_builder,
                                             multiple_rl_problem_instances& rl_problems,
                                             const solver_config& solver_cfg, const constraints_config_set& constraints,
                                             const rl_problem_behavioral_overview& behavioral_overview) {
  // std::unordered_map constructors accepting stateful hash/equal_to implementations also require the initial bucket
  // count as first parameter. Since we don't know the number of keys a priori, and since the map will automatically
  // add more buckets if needed, we can just take a (small) sensible default value.
  static constexpr auto DEFAULT_BUCKET_COUNT{1};
  rl_sparse_solutions_type result(DEFAULT_BUCKET_COUNT, constraints_behavioral_hash{behavioral_overview},
                                  constraints_equal_to{behavioral_overview});

  static constexpr auto solve_and_update_result{
      [](const auto& problem_builder, auto& rl_problems, const auto& config, auto& result) {
        rl_problems.assign_compatibilities();
        rl_problems.solve_problem_instance(config);
        auto solutions{problem_builder.extract_solutions(rl_problems)};
        for (size_t i{}; i != solutions.size(); ++i) {
          const auto result_it{result.find(rl_problems.get_slot(i))};
          debug_assert(result_it != end(result));
          debug_assert(result_it->second.empty());  // check that for all degenerate constraints, we only align once
          result_it->second = std::move(solutions[i]);
        }
      }};

  for (const auto& constraint : constraints.data) {
    if (result.find(constraint) != end(result)) {
      continue;
    }
    if (!rl_problems.has_free_slot()) {
      solve_and_update_result(problem_builder, rl_problems, solver_cfg, result);
      rl_problems.reset_slots();
    }
    rl_problems.add_compatibilities(constraint);
    result.try_emplace(constraint);
  }
  solve_and_update_result(problem_builder, rl_problems, solver_cfg, result);
  return result;
}

std::tuple<trace_alignment, std::vector<size_t>, size_t> log_aligner::get_optimal_alignment_and_statistics(
    std::span<const row_id> pruned_mapped_trace, const petri_net::petri_net_accessor& pn_accessor_tt,
    const constraints_config_set& constraints, gap_filler& filler, const common::execution_context& context) const {
  size_t optimizations_solved{0};
  rl_problem_instance_builder problem_builder{pruned_mapped_trace.size()};
  auto rl_problems{problem_builder.build_problem(pn_accessor_tt, rl_align_cfg_.problem_building_cfg, pn_information_,
                                                 pruned_mapped_trace)};
  const auto behavioral_overview{rl_problems.compute_behavioral_overview()};
  const auto constraints_are_dense{
      behavioral_overview.has_right_order_constraint && behavioral_overview.has_wrong_order_constraint &&
      behavioral_overview.has_exclusive_constraint && behavioral_overview.has_parallel_constraint &&
      behavioral_overview.has_deletion_constraint};
  std::vector<size_t> alignment_scores(constraints.data.size(), std::numeric_limits<size_t>::max());
  trace_alignment best_alignment{};
  if (constraints_are_dense) {
    size_t best_cost_so_far{std::numeric_limits<size_t>::max()};
    const auto dense_solutions{rl_solutions_dense(problem_builder, rl_problems, rl_align_cfg_.solver_cfg, constraints)};
    optimizations_solved = dense_solutions.size();
    // complete alignment and save alignment scores
    for (size_t i{0}; i != dense_solutions.size(); ++i) {
      const auto& solution{dense_solutions[i]};
      if (get_cost(solution) >= best_cost_so_far) {
        continue;
      }
      auto alignment{filler.complete_alignment(solution, pruned_mapped_trace, context)};
      alignment.prune_simple();
      const auto alignment_cost{alignment.cost()};
      if (alignment_cost < best_cost_so_far) {
        best_cost_so_far = alignment_cost;
        best_alignment = std::move(alignment);
      }
      alignment_scores[i] = alignment_cost;
    }
  } else {
    size_t best_cost_so_far{std::numeric_limits<size_t>::max()};
    const auto sparse_solutions{
        rl_solutions_sparse(problem_builder, rl_problems, rl_align_cfg_.solver_cfg, constraints, behavioral_overview)};
    // complete alignment and save alignment scores
    optimizations_solved = sparse_solutions.size();
    for (size_t i{0}; i != constraints.data.size(); ++i) {
      const auto& constraint{constraints.data[i]};
      const auto solution_iterator{sparse_solutions.find(constraint)};
      debug_assert(solution_iterator != end(sparse_solutions));
      const auto& solution{solution_iterator->second};
      if (get_cost(solution) >= best_cost_so_far) {
        continue;
      }
      auto alignment{filler.complete_alignment(solution, pruned_mapped_trace, context)};
      alignment.prune_simple();
      const auto alignment_cost{alignment.cost()};
      if (alignment_cost < best_cost_so_far) {
        best_cost_so_far = alignment_cost;
        best_alignment = std::move(alignment);
      }
      alignment_scores[i] = alignment_cost;
    }
  }
  // TODO (goulart.e) collect all the timeout constants in a single place
  best_alignment.prune_lcs(rl_align_cfg_.lcs_chunk_size, context);
  return {best_alignment, alignment_scores, optimizations_solved};
}

std::pair<vector_of_alignments, log_aligner::agg_alignment_counters> log_aligner::execute(
    common::execution_context& context, const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
    std::string_view operator_name) {
  vector_of_alignments pruned_alignments(
      trace_alignment_allocator_type{std::make_shared<ctl::batched_tracking_memory_resource>()});
  const row_id number_of_pruned_variants{pruned_variant_trace_cache->get_num_traces()};
  pruned_alignments.resize(number_of_pruned_variants);
  tbb::enumerable_thread_specific<log_aligner::alignment_counters> counters{};

  const auto pruned_traces{pruned_variant_trace_cache->get_traces(context)};
  const auto pruned_trace_lengths{pruned_variant_trace_cache->get_trace_lengths(context)};

  // Main loop. For each variant run grid search and pick the best
  // Compute fallback: Basically a shortest Petri net run, and a method to align a sequence to it
  const auto shortest_run{get_shortest_pn_trace(pn_data_tt_, pn_information_, operator_name, context)};
  if (!shortest_run) {
    context.add_warning(fmt::format(
        "{}: Petri net is very complex. Some alignments may be missing some required model moves.", operator_name));
  }
  auto fallback_aligner{shortest_run.has_value()
                            ? std::optional<sequence_aligner>{{shortest_run.value(), num_a_star_iterations_}}
                            : std::nullopt};

  constraints_config_set constraint_set{rl_align_cfg_.constraints_cfg_grid.make_full_grid()};
  // TODO(goulart.e) Check if different grain sizes change performance
  tbb::enumerable_thread_specific<gap_filler> gap_fillers{pn_data_tt_, rl_align_cfg_.solver_cfg.bfs_max_depth,
                                                          pn_information_.shortest_paths_tt, fallback_aligner};
  tbb::enumerable_thread_specific<rl_statistics::top_constraints> statistics_aggregators{};
  tbb::enumerable_thread_specific<rl_statistics::optimal_alignment_set_cover> set_cover_aggregators{};
  const auto align_range_with_statistics{
      get_range_aligner(*this, pruned_alignments, pruned_dict_mapping_, counters, pruned_traces, pruned_trace_lengths,
                        constraint_set, gap_fillers, context, statistics_aggregators, set_cover_aggregators)};
  // TODO(a.swoboda) We may have some bias here, as the traces are sorted and we're only analysing the first ones
  tbb::parallel_for(tbb::blocked_range<row_id>{0, std::min(number_of_pruned_variants, NUM_ANALYSIS_RUNS)},
                    align_range_with_statistics);

  // select best constraints according to our strategies
  rl_statistics::top_constraints merged_statistics{};
  std::ranges::for_each(statistics_aggregators,
                        [&merged_statistics](const auto& s) { return merged_statistics.update(s); });
  auto reduced_constraint_set{merged_statistics.get_top(NUM_CONSTRAINTS_KEPT)};
  rl_statistics::optimal_alignment_set_cover merged_covering_statistics{};
  std::ranges::for_each(set_cover_aggregators,
                        [&merged_covering_statistics](const auto& s) { return merged_covering_statistics.update(s); });
  auto covering_constraints{std::move(merged_covering_statistics).get_approximate_cover()};
  // merge the resulting constraints
  extend_with(reduced_constraint_set.data, covering_constraints.data);
  std::ranges::sort(reduced_constraint_set.data);
  // TODO (bluppes): use std::ranges::unique. There currently is a bug in clang-14 that prevents this from compiling.
  reduced_constraint_set.data.erase(std::unique(begin(constraint_set.data), end(constraint_set.data)),
                                    end(constraint_set.data));

  const auto& nonempty_constraint_set{empty(reduced_constraint_set.data) ? constraint_set : reduced_constraint_set};

  // align the remaining traces with the reduced number of constraints
  const auto align_range{get_range_aligner(*this, pruned_alignments, pruned_dict_mapping_, counters, pruned_traces,
                                           pruned_trace_lengths, nonempty_constraint_set, gap_fillers, context,
                                           statistics_aggregators)};
  tbb::parallel_for(
      tbb::blocked_range<row_id>{std::min(number_of_pruned_variants, NUM_ANALYSIS_RUNS), number_of_pruned_variants},
      align_range);

  auto agg_counters{std::accumulate(std::cbegin(counters), std::cend(counters), alignment_counters{})};

  rl_statistics::top_constraints final_statistics{};
  std::ranges::for_each(statistics_aggregators,
                        [&final_statistics](const auto& s) { return final_statistics.update(s); });

  auto best_constraints{final_statistics.get_all_best()};
  static constexpr size_t MAX_CONSTRAINT_SETS_TO_LOG{5};
  if (size(best_constraints.data) > MAX_CONSTRAINT_SETS_TO_LOG) {
    best_constraints.data.resize(MAX_CONSTRAINT_SETS_TO_LOG);
  }

  return {pruned_alignments, {agg_counters, best_constraints}};
}

std::pair<trace_alignment, log_aligner::alignment_counters> log_aligner::align_variant(
    std::span<const row_id> pruned_mapped_trace, gap_filler& filler, const constraints_config_set& constraints,
    const std::vector<rl_statistics::statistics_base*>& statistics, const common::execution_context& context) const {
  petri_net::petri_net_accessor pn_accessor_tt{pn_data_tt_};
  alignment_counters trace_counters{};

  // Only align non empty variants
  if (pruned_mapped_trace.empty()) {
    petri_net::rl_problem_solution_t solution{};
    auto alignment{filler.complete_alignment(solution, pruned_mapped_trace, context)};
    return {alignment, trace_counters};
  }

  const auto prefix_alignment{align_fitting_prefix(pn_accessor_tt, pruned_mapped_trace, context)};
  if (prefix_alignment) {
    return {prefix_alignment.value(), trace_counters};
  }

  // try A*-search on the synchronous product for a few iterations
  common::timer timer_optimal{};
  auto optimal_alignment{search_synchronous_product_for_optimal_alignment(pn_accessor_tt, pruned_mapped_trace,
                                                                          num_a_star_iterations_, context)};
  timer_optimal.stop();
  trace_counters.time_optimal = timer_optimal.duration_us().count();

  if (optimal_alignment.has_value()) {
    trace_counters.pruned_variants_computed_optimal = 1;
    return {optimal_alignment.value(), trace_counters};
  }

  trace_counters.pruned_variants_computed_relaxation_labeling = 1;
  common::timer timer_relaxation_labeling{};
  const auto [alignment, scores, optimizations_solved]{
      get_optimal_alignment_and_statistics(pruned_mapped_trace, pn_accessor_tt, constraints, filler, context)};
  timer_relaxation_labeling.stop();
  trace_counters.time_relaxation_labeling = timer_relaxation_labeling.duration_us().count();
  trace_counters.optimizations_solved = optimizations_solved;

  debug_assert(constraints.data.size() == scores.size());
  auto zip_begin{boost::make_zip_iterator(boost::make_tuple(constraints.data.begin(), scores.begin()))};
  const auto zip_end{boost::make_zip_iterator(boost::make_tuple(constraints.data.end(), scores.end()))};
  std::for_each(zip_begin, zip_end, [&statistics](const auto& t) {
    const auto scores_value{boost::get<1>(t)};
    if (scores_value < std::numeric_limits<size_t>::max()) {
      const auto& constraints_value{boost::get<0>(t)};
      std::ranges::for_each(statistics, [&constraints_value, scores_value](auto* const statistics_aggregator) {
        statistics_aggregator->update_current_run(constraints_value, scores_value);
      });
    }
  });
  std::ranges::for_each(statistics,
                        [](auto* const statistics_aggregator) { statistics_aggregator->finish_current_run(); });
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
  alignment_cost += rhs.alignment_cost;
  time_optimal += rhs.time_optimal;
  time_relaxation_labeling += rhs.time_relaxation_labeling;
  return *this;
}

std::tuple<vector_of_alignments, alignment_statistics, transition_id_to_str_mapping_t> compute_alignments(
    const petri_net::petri_net_representation& pn_repr, const std::unordered_set<std::string>& keep_transitions,
    const memory::cache::variant_trace_cache_t& pruned_variant_trace_cache,
    log_aligner::dict_mapping_type pruned_dict_mapping, int num_a_star_iterations, rl_align_config rl_align_cfg,
    common::execution_context& operator_context, const std::string& user_facing_name) {
  alignment_statistics stats{};
  const auto [visible_labels_count, distinct_visible_labels_count]{pn_repr.count_labels()};
  stats.petri_net_visible_label_count = visible_labels_count;
  stats.distinct_petri_net_visible_label_count = distinct_visible_labels_count;

  const auto reduced_pn_repr{petri_net::murata::reduce_murata(pn_repr, keep_transitions)};

  common::timer unfolding_timer{};
  auto unf_repr{compute_unfolding(reduced_pn_repr, user_facing_name)};
  unfolding_timer.stop();
  stats.time_unfolding = unfolding_timer.duration_us().count();

  common::timer preprocess_timer{};
  input_output_mapper io_mapper{};
  petri_net::safe_petri_net_data pn_data_tt{io_mapper, reconnect_unfolding(unf_repr, {true, true}), user_facing_name};
  petri_net::safe_petri_net_data pn_data_tf{io_mapper, reconnect_unfolding(unf_repr, {true, false}), user_facing_name};
  preprocess_timer.stop();
  stats.time_preprocess = preprocess_timer.duration_us().count();

  if (!reduced_pn_repr.transitions.empty() &&
      (pn_data_tt.final_markings.empty() || pn_data_tf.final_markings.empty())) {
    throw common::cpm_exception{"{}: The start and end places in the Petri net seem to be disconnected.",
                                user_facing_name};
  }

  log_aligner aligner{std::move(pn_data_tt),   std::move(pn_data_tf), pruned_dict_mapping,
                      std::move(rl_align_cfg), num_a_star_iterations, operator_context};
  const auto [pruned_alignments,
              perf_counters]{aligner.execute(operator_context, pruned_variant_trace_cache, user_facing_name)};

  stats.pruned_variants_computed_optimal += perf_counters.pruned_variants_computed_optimal;
  stats.pruned_variants_computed_relaxation_labeling += perf_counters.pruned_variants_computed_relaxation_labeling;
  stats.optimizations_solved += perf_counters.optimizations_solved;
  stats.total_cost_pruned_variants += perf_counters.alignment_cost;
  stats.time_optimal += perf_counters.time_optimal;
  stats.time_relaxation_labeling += perf_counters.time_relaxation_labeling;
  stats.pruned_variants_count = pruned_alignments.size();

  std::ranges::copy(perf_counters.best_compatibilities.data, std::back_inserter(stats.constraints));

  // Build the mapping event_int_id => transition_str_id
  transition_id_to_str_mapping_t mapping{};
  for (const auto& [event_str_id, _] : unf_repr.petri_net_repr.transitions) {
    const auto event_id{io_mapper.get_transition_for_str_id(event_str_id)};
    const auto transition_str_id{unf_repr.event_id_to_original_node_id.at(event_str_id)};
    mapping.emplace(event_id, transition_str_id);
  }

  return {pruned_alignments, stats, mapping};
}

}  // namespace celonis::accelerator::operators::process::alignment
