#include "rl_aligner.h"

#include <boost/iterator/zip_iterator.hpp>
#include <boost/range/iterator_range.hpp>

#include "modules/operators/process/alignment/rl_align/relaxation_labeling.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

namespace {

template <typename T>
void extend_with(std::vector<T>& v, std::vector<T>&& w) {
  v.insert(end(v), std::make_move_iterator(begin(w)), std::make_move_iterator(end(w)));
}

template <typename T>
void extend_with(std::vector<T>& v, const std::vector<T>& w) {
  v.insert(end(v), begin(w), end(w));
}

// TODO (goulart.e) solver_config should already be passed to the multiple_rl_instances_solver
std::vector<petri_net::rl_problem_solution_t> rl_solutions_dense(const rl_problem_instance_builder& problem_builder,
                                                                 multiple_rl_problem_instances& rl_problems,
                                                                 const rl_align::solver_config& solver_cfg,
                                                                 const rl_align::constraints_config_set& constraints) {
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
          legacy_embedded_debug_assert(result_it != end(result));
          legacy_embedded_debug_assert(result_it->second.empty());  // check that for all degenerate constraints, we only align once
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

}  // namespace

rl_aligner::rl_aligner(const petri_net::safe_petri_net_data& pn_data_tt,
                       const petri_net::safe_petri_net_data& pn_data_tf, const petri_net_information& pn_info,
                       row_id num_analysis_run, row_id num_constraints_kept, rl_align_config cfg,
                       const common::execution_context& ctx)
    : pn_tt_{pn_data_tt, ctx},
      pn_tf_{pn_data_tf, ctx},
      pn_info_{pn_info},
      num_analysis_run_{num_analysis_run},
      num_constraints_kept_{num_constraints_kept},
      cfg_{std::move(cfg)},
      gap_filler_{pn_data_tt, cfg_.solver_cfg.bfs_max_iterations, cfg_.solver_cfg.bfs_max_insertions,
                  pn_info.transition_distances_tt, ctx.create_sub_context_with_same_span()},
      constraints_{cfg_.constraints_cfg_grid.make_full_grid()} {}

std::pair<trace_alignment_t, size_t> rl_aligner::operator()(std::span<const row_id> variant, int max_cost,
                                                            const common::execution_context& context) {
  select_best_constraints_if_enough_data();

  size_t optimizations_solved{0};
  rl_align::rl_problem_instance_builder problem_builder{variant.size()};
  auto rl_problems{problem_builder.build_problem(pn_tt_, cfg_.problem_building_cfg, pn_info_, variant)};
  const auto behavioral_overview{rl_problems.compute_behavioral_overview()};
  const auto constraints_are_dense{
      behavioral_overview.has_right_order_constraint && behavioral_overview.has_wrong_order_constraint &&
      behavioral_overview.has_exclusive_constraint && behavioral_overview.has_parallel_constraint &&
      behavioral_overview.has_deletion_constraint};
  std::vector<size_t> alignment_scores(constraints_.data.size(), std::numeric_limits<size_t>::max());
  trace_alignment_t best_alignment{std::nullopt};

  if (constraints_are_dense) {
    size_t best_cost_so_far{std::numeric_limits<size_t>::max()};
    const auto dense_solutions{rl_solutions_dense(problem_builder, rl_problems, cfg_.solver_cfg, constraints_)};
    optimizations_solved = dense_solutions.size();
    // complete alignment and save alignment scores
    for (size_t i{0}; i != dense_solutions.size(); ++i) {
      const auto& solution{dense_solutions[i]};
      if (get_cost(solution) >= best_cost_so_far) {
        continue;
      }
      if (auto alignment{gap_filler_.complete_alignment(solution, variant, max_cost, context)}; alignment) {
        alignment.value().prune_simple();
        const auto alignment_cost{alignment.value().cost()};
        if (alignment_cost < best_cost_so_far) {
          best_cost_so_far = alignment_cost;
          best_alignment = std::move(alignment);
        }
        alignment_scores[i] = alignment_cost;
      }
    }
  } else {
    size_t best_cost_so_far{std::numeric_limits<size_t>::max()};
    const auto sparse_solutions{
        rl_solutions_sparse(problem_builder, rl_problems, cfg_.solver_cfg, constraints_, behavioral_overview)};
    // complete alignment and save alignment scores
    optimizations_solved = sparse_solutions.size();
    for (size_t i{0}; i != constraints_.data.size(); ++i) {
      const auto& constraint{constraints_.data[i]};
      const auto solution_iterator{sparse_solutions.find(constraint)};
      legacy_embedded_debug_assert(solution_iterator != end(sparse_solutions));
      const auto& solution{solution_iterator->second};
      if (get_cost(solution) >= best_cost_so_far) {
        continue;
      }
      if (auto alignment{gap_filler_.complete_alignment(solution, variant, max_cost, context)}; alignment) {
        alignment.value().prune_simple();
        const auto alignment_cost{alignment.value().cost()};
        if (alignment_cost < best_cost_so_far) {
          best_cost_so_far = alignment_cost;
          best_alignment = std::move(alignment);
        }
        alignment_scores[i] = alignment_cost;
      }
    }
  }

  if (best_alignment) {
    // TODO (goulart.e) collect all the timeout constants in a single place
    best_alignment.value().prune_lcs(cfg_.lcs_chunk_size, context);
  }

  collect_statistics(alignment_scores);
  ++alignments_so_far_;

  return {best_alignment, optimizations_solved};
}

void rl_aligner::collect_statistics(const std::vector<size_t>& alignment_scores) {
  legacy_embedded_debug_assert(constraints_.data.size() == alignment_scores.size());
  auto zip_begin{boost::make_zip_iterator(boost::make_tuple(constraints_.data.begin(), alignment_scores.begin()))};
  const auto zip_end{boost::make_zip_iterator(boost::make_tuple(constraints_.data.end(), alignment_scores.end()))};

  for (const auto& [constraints_value, scores_value] : boost::make_iterator_range(zip_begin, zip_end)) {
    if (scores_value < std::numeric_limits<size_t>::max()) {
      statistics_aggregator_.update_current_run(constraints_value, scores_value);
      set_cover_aggregator_.update_current_run(constraints_value, scores_value);
    }
  }
  statistics_aggregator_.finish_current_run();
}

void rl_aligner::select_best_constraints_if_enough_data() {
  // TODO (goulart.e) This is not deterministic anymore, since each rl_aligner might get a different set of optimized
  //  constraints. Because of that, we disable this code until we can benchmarks whether this optimization is worth
  //  the trouble. See (CPL-9596)
  if (alignments_so_far_ != num_analysis_run_ || alignments_so_far_ >= 0) {
    return;
  }

  auto reduced_constraint_set{statistics_aggregator_.get_top(num_constraints_kept_)};
  auto covering_constraints{set_cover_aggregator_.get_approximate_cover()};
  extend_with(reduced_constraint_set.data, covering_constraints.data);
  std::ranges::sort(reduced_constraint_set.data);

  reduced_constraint_set.data.erase(
      std::unique(std::begin(reduced_constraint_set.data), std::end(reduced_constraint_set.data)),
      std::end(reduced_constraint_set.data));
  if (!reduced_constraint_set.data.empty()) {
    constraints_ = reduced_constraint_set;
  }
}

[[nodiscard]] const rl_statistics::top_constraints& rl_aligner::statistics_aggregator() const {
  return statistics_aggregator_;
}

}  // namespace celonis::accelerator::operators::process::alignment::rl_align
