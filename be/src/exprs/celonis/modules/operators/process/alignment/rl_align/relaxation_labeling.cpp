#include "relaxation_labeling.h"

#include <algorithm>
#include <cmath>

#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

namespace {

rl_weight_t diff_abs(rl_weight_t a, rl_weight_t b) { return std::fabs(a - b); }

}  // namespace

multiple_rl_problem_instances::multiple_rl_problem_instances(size_t trace_length)
    : normalizations_(trace_length, rl_weights_slots_t{}) {
  slot_constraints_.fill({});
}

void multiple_rl_problem_instances::add_assignment(continuous_assignment assignment,
                                                   petri_net::trace_variable target_variable, bool is_null) {
  // Continuous assignments must be added in increasing order
  legacy_embedded_debug_assert(assignment.id == assignment_to_variable_.size());
  assignment_to_variable_.push_back(target_variable);
  is_null_assignment_.push_back(is_null);

  // std::array ensures initialization to 0.0
  supports_.emplace_back();
  assignment_normalizations_.emplace_back();
  assignment_weights_.emplace_back();
}

void multiple_rl_problem_instances::add_compatibilities(const constraints_config& constraints_cfg) {
  legacy_embedded_debug_assert(has_free_slot());

  slot_constraints_[constraints_config::RIGHT_ORDER_COMPATIBILITY_OFFSET][slot_offset_] =
      constraints_cfg.right_order_compatibility;
  slot_constraints_[constraints_config::WRONG_ORDER_COMPATIBILITY_OFFSET][slot_offset_] =
      constraints_cfg.wrong_order_compatibility;
  slot_constraints_[constraints_config::EXCLUSIVE_COMPATIBILITY_OFFSET][slot_offset_] =
      constraints_cfg.exclusive_compatibility;
  slot_constraints_[constraints_config::PARALLEL_COMPATIBILITY_OFFSET][slot_offset_] =
      constraints_cfg.parallel_compatibility;
  slot_constraints_[constraints_config::DELETION_COMPATIBILITY_OFFSET][slot_offset_] =
      constraints_cfg.deletion_compatibility;

  slot_offset_++;
}

void multiple_rl_problem_instances::assign_compatibilities() {
  auto assign_slots_compatibilities{
      [](auto* __restrict__ slots_compatibilities, const auto* __restrict__ slots_constraints, auto multiplier) {
        for (size_t j{0}; j < NUMBER_OF_SLOTS; ++j) {
          slots_compatibilities[j] = multiplier * slots_constraints[j];
        }
      }};

  // TODO (goulart.e) the compatibility offsets can be computed once per trace
  for (size_t i{0}; i < pair_constraints_.size(); ++i) {
    const auto compatibility_info{pair_constraints_compatibility_infos_[i]};
    const auto multiplier{compatibility_info.multiplier};
    const auto compat_offset{constraints_config::get_offset_for_compatibility_type(compatibility_info.compat_type)};

    assign_slots_compatibilities(pair_constraints_compatibilities_[i].data(), slot_constraints_[compat_offset].data(),
                                 multiplier);
  }

  for (size_t i{0}; i < triple_constraints_.size(); ++i) {
    const auto compatibility_info{triple_constraints_compatibility_infos_[i]};
    const auto multiplier{compatibility_info.multiplier};
    const auto compat_offset{constraints_config::get_offset_for_compatibility_type(compatibility_info.compat_type)};

    assign_slots_compatibilities(triple_constraints_compatibilities_[i].data(), slot_constraints_[compat_offset].data(),
                                 multiplier);
  }
}

void multiple_rl_problem_instances::solve_problem_instance(const solver_config& solver_cfg) {
  // Main loop of the algorithm
  initialize_weights(solver_cfg.null_label_bias);

  for (size_t iteration_count{0}; iteration_count < solver_cfg.max_iterations; ++iteration_count) {
    next_iteration();
    compute_supports();
    clip_supports(solver_cfg);
    compute_normalizations();

    rl_weight_t max_delta{0.0};
    for (size_t i{0}; i < assignment_weights_.size(); ++i) {
      // Can't do j < NUMBER_OF_SLOTS because the uninitialized slots
      // would mess up the division and the delta computation
      for (size_t j{0}; j < slot_offset_; ++j) {
        const auto assignment_weight{assignment_weights_[i][j]};
        const auto assignment_support{supports_[i][j]};

        const auto assignment_normalization{assignment_normalizations_[i][j]};
        const auto new_weight{assignment_weight * assignment_support / assignment_normalization};

        assignment_weights_[i][j] = new_weight;
        const auto delta{std::fabs(new_weight - assignment_weight)};
        if (max_delta < solver_cfg.min_delta && delta > max_delta) {
          max_delta = delta;
        }
      }
    }

    if (max_delta < solver_cfg.min_delta) {
      break;
    }
  }
}

bool multiple_rl_problem_instances::has_free_slot() const noexcept { return slot_offset_ < NUMBER_OF_SLOTS; }

void multiple_rl_problem_instances::reset_slots() noexcept { slot_offset_ = 0; }

void multiple_rl_problem_instances::add_pair_constraint(const pair_constraint& constraint,
                                                        const compatibility_information& compatibility_info) {
  pair_constraints_.emplace_back(constraint);
  pair_constraints_compatibility_infos_.emplace_back(compatibility_info);
  pair_constraints_compatibilities_.emplace_back();
}

void multiple_rl_problem_instances::add_triple_constraint(const triple_constraint& constraint,
                                                          const compatibility_information& compatibility_info) {
  triple_constraints_.emplace_back(constraint);
  triple_constraints_compatibility_infos_.emplace_back(compatibility_info);
  triple_constraints_compatibilities_.emplace_back();
}

rl_problem_behavioral_overview multiple_rl_problem_instances::compute_behavioral_overview() const noexcept {
  rl_problem_behavioral_overview result{};

  auto add_compatibility_type_lmb{[&result](const auto& compat_type) {
    switch (compat_type) {
      using enum compatibility_type;
      case RIGHT_ORDER:
        result.has_right_order_constraint = true;
        break;
      case WRONG_ORDER:
        result.has_wrong_order_constraint = true;
        break;
      case EXCLUSIVE:
        result.has_exclusive_constraint = true;
        break;
      case PARALLEL:
        result.has_parallel_constraint = true;
        break;
      case DELETION:
        result.has_deletion_constraint = true;
        break;
    }
  }};

  for (const auto& pair_compat_info : pair_constraints_compatibility_infos_) {
    add_compatibility_type_lmb(pair_compat_info.compat_type);
  }
  for (const auto& triple_compat_info : triple_constraints_compatibility_infos_) {
    add_compatibility_type_lmb(triple_compat_info.compat_type);
  }

  return result;
}

void multiple_rl_problem_instances::initialize_weights(rl_weight_t null_label_bias) noexcept {
  for (size_t i{0}; i < assignment_weights_.size(); ++i) {
    auto& slots_weights{assignment_weights_[i]};
    std::ranges::fill(slots_weights, is_null_assignment_[i] ? null_label_bias : 1.0f);
  }

  for (size_t i{0}; i < assignment_weights_.size(); ++i) {
    const auto variable{assignment_to_variable_[i]};

    for (size_t j{0}; j < NUMBER_OF_SLOTS; ++j) {
      const auto assignment_weight{assignment_weights_[i][j]};
      if (assignment_weight != null_label_bias) {
        normalizations_[variable.id][j] += assignment_weight;
      }
    }
  }

  for (size_t i{0}; i < assignment_weights_.size(); ++i) {
    const auto variable{assignment_to_variable_[i]};

    for (size_t j{0}; j < NUMBER_OF_SLOTS; ++j) {
      const auto assignment_weight{assignment_weights_[i][j]};
      if (assignment_weight != null_label_bias) {
        assignment_weights_[i][j] /= normalizations_[variable.id][j];
        assignment_weights_[i][j] *= (1 - null_label_bias);
      }
    }
  }
}

void multiple_rl_problem_instances::next_iteration() {
  for (auto& slot_supports : supports_) {
    std::ranges::fill(slot_supports, 0.0);
  }
  for (auto& slot_normalizations : normalizations_) {
    std::ranges::fill(slot_normalizations, 0.0);
  }
}

void multiple_rl_problem_instances::compute_supports() {
  // We pack this code into a lambda so that the compiler can auto-vectorize it
  auto compute_supports_pair_constraints{[](const auto* __restrict__ slots_compatibilities_ptr,
                                            const auto* __restrict__ slots_cond_assignment_weights_ptr,
                                            auto* __restrict__ slots_supports_ptr) {
    for (size_t j{0}; j < NUMBER_OF_SLOTS; ++j) {
      const auto compatibility{slots_compatibilities_ptr[j]};
      const auto cond_assignment_prob{slots_cond_assignment_weights_ptr[j]};
      slots_supports_ptr[j] += compatibility * cond_assignment_prob;
    }
  }};

  for (size_t i{0}; i < pair_constraints_.size(); ++i) {
    const auto [tgt_assignment, cond_assignment]{pair_constraints_[i]};
    compute_supports_pair_constraints(pair_constraints_compatibilities_[i].data(),
                                      assignment_weights_[cond_assignment.id].data(),
                                      supports_[tgt_assignment.id].data());
  }

  // We pack this code into a lambda so that the compiler can auto-vectorize it
  auto compute_supports_triple_constraints{
      [](const auto* __restrict__ slots_compatibilities_ptr, const auto* __restrict__ slots_cond_assignment_prob_1_ptr,
         const auto* __restrict__ slots_cond_assignment_prob_2_ptr, auto* __restrict__ slots_supports_ptr) {
        for (size_t j{0}; j < NUMBER_OF_SLOTS; ++j) {
          const auto compatibility{slots_compatibilities_ptr[j]};
          const auto cond_assignment_1_prob{slots_cond_assignment_prob_1_ptr[j]};
          const auto cond_assignment_2_prob{slots_cond_assignment_prob_2_ptr[j]};
          slots_supports_ptr[j] += compatibility * cond_assignment_1_prob * cond_assignment_2_prob;
        }
      }};

  // Compute the support for triple constraints
  for (size_t i{0}; i < triple_constraints_.size(); ++i) {
    const auto& [tgt_assignment, cond_assignment_1, cond_assignment_2]{triple_constraints_[i]};
    compute_supports_triple_constraints(
        triple_constraints_compatibilities_[i].data(), assignment_weights_[cond_assignment_1.id].data(),
        assignment_weights_[cond_assignment_2.id].data(), supports_[tgt_assignment.id].data());
  }
}

void multiple_rl_problem_instances::clip_supports(const solver_config& solver_cfg) noexcept {
  // We pack it as a lambda so the compiler can vectorize
  auto clip_slots_supports{// Compute the multiplication of the inverse because this is faster
                           [support_range = 1.0f / solver_cfg.supports_range](auto* __restrict__ slot_supports_ptr) {
                             for (size_t i{0}; i < NUMBER_OF_SLOTS; ++i) {
                               const auto support_value{slot_supports_ptr[i]};
                               slot_supports_ptr[i] = std::clamp(support_value * support_range, -1.0f, 1.0f) + 1.0f;
                             }
                           }};

  for (auto& slot_supports : supports_) {
    clip_slots_supports(slot_supports.data());
  }
}

void multiple_rl_problem_instances::compute_normalizations() noexcept {
  auto compute_slots_normalization{[](const auto* __restrict__ slots_assignment_weights_ptr,
                                      const auto* __restrict__ slots_supports_ptr,
                                      auto* __restrict__ slots_normalizations_ptr) {
    for (size_t j{0}; j < NUMBER_OF_SLOTS; ++j) {
      const auto assignment_weight{slots_assignment_weights_ptr[j]};
      const auto assignment_support{slots_supports_ptr[j]};
      slots_normalizations_ptr[j] += assignment_weight * assignment_support;
    }
  }};

  for (size_t i{0}; i < assignment_weights_.size(); ++i) {
    const auto assignment_variable{assignment_to_variable_[i]};
    compute_slots_normalization(assignment_weights_[i].data(), supports_[i].data(),
                                normalizations_[assignment_variable.id].data());
  }

  for (size_t i{0}; i < assignment_weights_.size(); ++i) {
    const auto assignment_variable{assignment_to_variable_[i]};

    for (size_t j{0}; j < slot_offset_; ++j) {
      assignment_normalizations_[i][j] = normalizations_[assignment_variable.id][j];
    }
  }
}

rl_problem_instance_builder::rl_problem_instance_builder(size_t trace_length) : trace_length_{trace_length} {}

multiple_rl_problem_instances rl_problem_instance_builder::build_problem(
    const petri_net::petri_net_accessor& pn_accessor, const problem_building_config& problem_building_cfg,
    const petri_net_information& pn_info, std::span<const row_id> pruned_mapped_trace) {
  multiple_rl_problem_instances result_instance{trace_length_};
  build_trace_object(result_instance, pn_accessor, pruned_mapped_trace);

  if (trace_obj_.empty()) {
    return result_instance;
  }

  build_null_constraints(result_instance);
  build_pair_constraints(result_instance, problem_building_cfg, pn_info);
  build_triple_constraints(result_instance, pn_info);
  return result_instance;
}

std::vector<petri_net::rl_problem_solution_t> rl_problem_instance_builder::extract_solutions(
    const multiple_rl_problem_instances& problem_instance) const {
  std::vector<petri_net::rl_problem_solution_t> solutions{};

  for (size_t slot_offset{0}; slot_offset < problem_instance.slot_offset_; ++slot_offset) {
    // Initialize to -1 to ensure that at least one assignment will modify the default assignment value
    std::vector<rl_weight_t> highest_weights(trace_obj_.size(), -1.0);
    petri_net::rl_problem_solution_t solution(trace_obj_.size());

    for (size_t i{0}; i < problem_instance.assignment_weights_.size(); ++i) {
      const auto assignment_variable{problem_instance.assignment_to_variable_[i]};
      const auto assignment_weight{problem_instance.assignment_weights_[i][slot_offset]};

      if (highest_weights[assignment_variable.id] < assignment_weight) {
        highest_weights[assignment_variable.id] = assignment_weight;
        continuous_assignment assignment{static_cast<uint32_t>(i)};
        solution[assignment_variable.id] = continuous_mapper_[assignment];
      }
    }
    solutions.emplace_back(std::move(solution));
  }

  return solutions;
}

void rl_problem_instance_builder::build_trace_object(multiple_rl_problem_instances& result_instance,
                                                     const petri_net::petri_net_accessor& pn_accessor,
                                                     std::span<const row_id> pruned_mapped_trace) {
  trace_obj_.reserve(trace_length_);

  for (size_t variable_id{0}; variable_id < trace_length_; ++variable_id) {
    const auto event_label{pruned_mapped_trace[variable_id]};
    petri_net::trace_variable variable{static_cast<uint16_t>(variable_id)};

    const auto transitions{pn_accessor.get_transitions_for_label(event_label)};
    std::vector<continuous_assignment> continuous_assignments{};

    for (const auto& transition : transitions) {
      const auto assignment{petri_net::variable_to_transition_assignment(variable, transition)};
      const auto continuous_assignment{continuous_mapper_.get_continuous_assignment(assignment)};
      continuous_assignments.emplace_back(continuous_assignment);
      result_instance.add_assignment(continuous_assignment, variable, false);
    }

    trace_obj_.emplace_back(std::move(continuous_assignments), variable, event_label);
  }
}

void rl_problem_instance_builder::build_null_constraints(multiple_rl_problem_instances& result_instance) {
  const auto null_transition{petri_net::petri_net_transition_id::create_null_transition()};
  for (const auto& variable_info : trace_obj_) {
    const auto assignment{petri_net::variable_to_transition_assignment(variable_info.variable, null_transition)};
    const auto continuous_assignment{continuous_mapper_.get_continuous_assignment(assignment)};
    result_instance.add_assignment(continuous_assignment, variable_info.variable, true);
  }
}

void rl_problem_instance_builder::build_pair_constraints(multiple_rl_problem_instances& result_instance,
                                                         const problem_building_config& problem_building_cfg,
                                                         const petri_net_information& pn_info) {
  const auto& transition_distances_tt{pn_info.transition_distances_tt};
  const auto& behavioral_relations_tt{pn_info.behavioral_relations_tt};
  const auto& behavioral_relations_tf{pn_info.behavioral_relations_tf};

  for (size_t i{0}; i < trace_obj_.size(); ++i) {
    const auto j_limit{std::min(trace_obj_.size(), i + problem_building_cfg.maximum_distance)};
    for (size_t j{i + 1}; j < j_limit; ++j) {
      const auto& variable_i{trace_obj_[i]};
      const auto& variable_j{trace_obj_[j]};

      // Add all possible assignment combinations
      for (const auto& assignment_i_continuous : variable_i.continuous_assignments) {
        for (const auto& assignment_j_continuous : variable_j.continuous_assignments) {
          const auto transition_i{continuous_mapper_[assignment_i_continuous].tgt_transition};
          const auto transition_j{continuous_mapper_[assignment_j_continuous].tgt_transition};

          const pair_constraint constraint_i_j{assignment_i_continuous, assignment_j_continuous};
          const pair_constraint constraint_j_i{assignment_j_continuous, assignment_i_continuous};

          auto relation_i_j{behavioral_relations_tt.get_relation(transition_i, transition_j)};

          switch (constexpr rl_weight_t one{1.0}; relation_i_j) {
            using enum petri_net::behavioral_relation;
            case EXCLUSIVE: {
              compatibility_information compatibility_info{one, compatibility_type::EXCLUSIVE};
              result_instance.add_pair_constraint(constraint_i_j, compatibility_info);
              result_instance.add_pair_constraint(constraint_j_i, compatibility_info);
              break;
            }
            case FOLLOWS: {
              compatibility_information compatibility_info{one, compatibility_type::WRONG_ORDER};
              result_instance.add_pair_constraint(constraint_i_j, compatibility_info);
              result_instance.add_pair_constraint(constraint_j_i, compatibility_info);
              break;
            }
            case PRECEDES: {
              const auto distance_variables{static_cast<rl_weight_t>(j - i)};
              const auto distance_labels{transition_distances_tt.get_distance(transition_i, transition_j)};
              auto dist_diff{diff_abs(distance_variables, static_cast<rl_weight_t>(distance_labels)) + one};

              const auto multiplier{one / dist_diff};

              compatibility_information compatibility_info{multiplier, compatibility_type::RIGHT_ORDER};
              result_instance.add_pair_constraint(constraint_i_j, compatibility_info);
              result_instance.add_pair_constraint(constraint_j_i, compatibility_info);
              break;
            }
            case INTERLEAVED: {
              if (behavioral_relations_tf.get_relation(transition_i, transition_j) ==
                  petri_net::behavioral_relation::INTERLEAVED) {
                // parallel constraint
                compatibility_information compatibility_info{one, compatibility_type::PARALLEL};
                result_instance.add_pair_constraint(constraint_i_j, compatibility_info);
                result_instance.add_pair_constraint(constraint_j_i, compatibility_info);
              } else {
                // loop constraint
                const auto distance_variables{static_cast<rl_weight_t>(j - i)};

                const auto distance_labels_i_j{transition_distances_tt.get_distance(transition_i, transition_j)};
                const auto dist_diff_i_j{diff_abs(distance_variables, static_cast<rl_weight_t>(distance_labels_i_j))};
                compatibility_information compatibility_info_i_j{one / (dist_diff_i_j + one),
                                                                 compatibility_type::PARALLEL};
                result_instance.add_pair_constraint(constraint_i_j, compatibility_info_i_j);

                const auto distance_labels_j_i{transition_distances_tt.get_distance(transition_j, transition_i)};
                const auto dist_diff_j_i{diff_abs(distance_variables, static_cast<rl_weight_t>(distance_labels_j_i))};
                compatibility_information compatibility_info_j_i{one / (dist_diff_j_i + 1),
                                                                 compatibility_type::PARALLEL};
                result_instance.add_pair_constraint(constraint_j_i, compatibility_info_j_i);
              }
              break;
            }
          }
        }
      }
    }
  }
}

void rl_problem_instance_builder::build_triple_constraints(multiple_rl_problem_instances& result_instance,
                                                           const petri_net_information& pn_info) {
  const auto& shortest_enabling_paths_tt{pn_info.transition_distances_tt};
  const auto& behavioral_profile_tf{pn_info.behavioral_relations_tf};

  const auto compute_transitions_distance_lmb{
      [&behavioral_profile_tf, &shortest_enabling_paths_tt](const auto& transition_from,
                                                            const auto& transition_to) -> rl_weight_t {
        if (behavioral_profile_tf.get_relation(transition_from, transition_to) ==
            petri_net::behavioral_relation::INTERLEAVED) {
          return 0.0;
        }
        if (shortest_enabling_paths_tt.get_distance(transition_from, transition_to) != -1) {
          return static_cast<rl_weight_t>(shortest_enabling_paths_tt.get_distance(transition_from, transition_to));
        }
        return -1.0;
      }};

  for (size_t i{1}; i < trace_obj_.size() - 1; ++i) {
    const auto& previous_variable{trace_obj_[i - 1]};
    const auto& current_variable{trace_obj_[i]};
    const auto& next_variable{trace_obj_[i + 1]};

    for (const auto& previous_assignment : previous_variable.continuous_assignments) {
      for (const auto& current_assignment : current_variable.continuous_assignments) {
        for (const auto& next_assignment : next_variable.continuous_assignments) {
          const auto previous_transition{continuous_mapper_[previous_assignment].tgt_transition};
          const auto current_transition{continuous_mapper_[current_assignment].tgt_transition};
          const auto next_transition{continuous_mapper_[next_assignment].tgt_transition};

          const auto distance_previous_current{
              compute_transitions_distance_lmb(previous_transition, current_transition)};
          const auto distance_current_next{compute_transitions_distance_lmb(current_transition, next_transition)};
          const auto distance_previous_next{compute_transitions_distance_lmb(previous_transition, next_transition)};

          if (distance_previous_current < 0 || distance_current_next < 0 || distance_previous_next < 0) {
            continue;
          }

          auto path_size_difference{distance_previous_current + distance_current_next - distance_previous_next};
          if (path_size_difference > 0) {
            triple_constraint constraint{current_assignment, previous_assignment, next_assignment};
            compatibility_information compatibility_info{path_size_difference, compatibility_type::DELETION};
            result_instance.add_triple_constraint(constraint, compatibility_info);
          }
        }
      }
    }
  }
}

}  // namespace celonis::accelerator::operators::process::alignment::rl_align
