#pragma once

#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "legacy_embedded_ctl/assert.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/petri_net_information.h"
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

struct continuous_assignment {
  uint32_t id;
  explicit constexpr continuous_assignment(uint32_t id) noexcept : id(id) {}
};

struct variable_information {
  std::vector<continuous_assignment> continuous_assignments{};
  petri_net::trace_variable variable{};
  row_id event_label{};

  variable_information(std::vector<continuous_assignment> continuous_assignments, petri_net::trace_variable variable,
                       row_id event_label)
      : continuous_assignments(std::move(continuous_assignments)), variable{variable}, event_label{event_label} {};
};

/**
 * Maps variable_to_transition_assignment to a continuous_assignment.
 * This allows one to store and retrieve assignment-data directly through its continuous id
 */
struct continuous_assignment_mapper {
 public:
  continuous_assignment_mapper() noexcept = default;

  continuous_assignment get_continuous_assignment(petri_net::variable_to_transition_assignment assignment) {
    // If assignment was previously mapped, we get an error
    legacy_embedded_debug_assert(std::find(std::cbegin(continuous_to_pair_), std::cend(continuous_to_pair_), assignment) ==
                 std::cend(continuous_to_pair_));

    const auto cur_size{static_cast<uint32_t>(continuous_to_pair_.size())};
    continuous_to_pair_.emplace_back(assignment);
    return continuous_assignment{cur_size};
  }

  const petri_net::variable_to_transition_assignment& operator[](continuous_assignment cont_assignment) const {
    legacy_embedded_debug_assert(cont_assignment.id < continuous_to_pair_.size());
    return continuous_to_pair_[cont_assignment.id];
  }

 private:
  std::vector<petri_net::variable_to_transition_assignment> continuous_to_pair_{};
};

struct compatibility_information {
  rl_weight_t multiplier;
  compatibility_type compat_type;

  constexpr compatibility_information(rl_weight_t multiplier, compatibility_type compat_type) noexcept
      : multiplier{multiplier}, compat_type{compat_type} {}
};

struct pair_constraint {
  continuous_assignment target_assignment;
  continuous_assignment conditional_assignment;

  constexpr pair_constraint(continuous_assignment tgt_assignment, continuous_assignment cond_assignment) noexcept
      : target_assignment{tgt_assignment}, conditional_assignment{cond_assignment} {}
};

struct triple_constraint {
  continuous_assignment target_assignment;
  continuous_assignment conditional_assignment_1;
  continuous_assignment conditional_assignment_2;

  constexpr triple_constraint(continuous_assignment tgt_assignment, continuous_assignment cond_assignment_1,
                              continuous_assignment cond_assignment_2) noexcept
      : target_assignment{tgt_assignment},
        conditional_assignment_1{cond_assignment_1},
        conditional_assignment_2{cond_assignment_2} {}
};

class rl_problem_instance_builder;

class multiple_rl_problem_instances {
  friend rl_problem_instance_builder;

  // Set this number to a multiple of 8. With AVX-512 this could be a multiple of 16, but we do not deploy
  // on any server supporting AVX-512
  static constexpr size_t NUMBER_OF_SLOTS{8};
  using rl_weights_slots_t = std::array<rl_weight_t, NUMBER_OF_SLOTS>;

 public:
  explicit multiple_rl_problem_instances(size_t trace_length);

  [[nodiscard]] rl_problem_behavioral_overview compute_behavioral_overview() const noexcept;

  void add_compatibilities(const constraints_config& constraints_cfg);
  void assign_compatibilities();

  void solve_problem_instance(const solver_config& solver_cfg);

  [[nodiscard]] bool has_free_slot() const noexcept;
  void reset_slots() noexcept;

  [[nodiscard]] constraints_config get_slot(size_t index) const noexcept {
    constraints_config result{};
    result.right_order_compatibility = slot_constraints_[constraints_config::RIGHT_ORDER_COMPATIBILITY_OFFSET][index];
    result.wrong_order_compatibility = slot_constraints_[constraints_config::WRONG_ORDER_COMPATIBILITY_OFFSET][index];
    result.exclusive_compatibility = slot_constraints_[constraints_config::EXCLUSIVE_COMPATIBILITY_OFFSET][index];
    result.parallel_compatibility = slot_constraints_[constraints_config::PARALLEL_COMPATIBILITY_OFFSET][index];
    result.deletion_compatibility = slot_constraints_[constraints_config::DELETION_COMPATIBILITY_OFFSET][index];
    return result;
  }

 private:
  void add_assignment(continuous_assignment assignment, petri_net::trace_variable target_variable, bool is_null);

  void add_pair_constraint(const pair_constraint& constraint, const compatibility_information& compatibility_info);
  void add_triple_constraint(const triple_constraint& constraint, const compatibility_information& compatibility_info);

  void initialize_weights(rl_weight_t null_label_bias) noexcept;
  void next_iteration();
  void compute_supports();
  void clip_supports(const solver_config& solver_cfg) noexcept;
  void compute_normalizations() noexcept;

  // Add compatibilities will just update this slot constraints.
  //  When solving, we use this slots to write multiple compatibilities at once
  std::array<rl_weights_slots_t, constraints_config::NUMBER_OF_VARIABLES> slot_constraints_{};
  size_t slot_offset_{0};

  /// Pair constraints
  std::vector<pair_constraint> pair_constraints_{};
  std::vector<rl_weights_slots_t> pair_constraints_compatibilities_{};
  std::vector<compatibility_information> pair_constraints_compatibility_infos_{};

  /// Triple constraints
  std::vector<triple_constraint> triple_constraints_{};
  std::vector<rl_weights_slots_t> triple_constraints_compatibilities_{};
  std::vector<compatibility_information> triple_constraints_compatibility_infos_{};

  /// Bitset telling if assignment is a NULL assignment.
  /// Used to reset null assignment weights with null bias
  std::vector<bool> is_null_assignment_{};

  /// Support of each assignment
  std::vector<rl_weights_slots_t> supports_{};
  /// Per-variable normalization (support sums)
  std::vector<rl_weights_slots_t> normalizations_{};
  /// Per-assignment normalization
  std::vector<rl_weights_slots_t> assignment_normalizations_{};
  /// Corresponding variable for each assignment (to sum supports)
  std::vector<petri_net::trace_variable> assignment_to_variable_{};
  /// Assignment weights
  std::vector<rl_weights_slots_t> assignment_weights_{};
};

class rl_problem_instance_builder {
  using trace_object_t = std::vector<variable_information>;

 public:
  explicit rl_problem_instance_builder(size_t trace_length);

  [[nodiscard]] multiple_rl_problem_instances build_problem(const petri_net::petri_net_accessor& pn_accessor,
                                                            const problem_building_config& problem_building_cfg,
                                                            const petri_net_information& pn_info,
                                                            std::span<const row_id> pruned_mapped_trace);

  /// Extract the assignments with the highest score from rl_problem_instance
  [[nodiscard]] std::vector<petri_net::rl_problem_solution_t> extract_solutions(
      const multiple_rl_problem_instances& problem_instance) const;

 private:
  void build_trace_object(multiple_rl_problem_instances& result_instance,
                          const petri_net::petri_net_accessor& pn_accessor,
                          std::span<const row_id> pruned_mapped_trace);
  void build_null_constraints(multiple_rl_problem_instances& result_instance);
  void build_pair_constraints(multiple_rl_problem_instances& result_instance,
                              const problem_building_config& problem_building_cfg,
                              const petri_net_information& pn_info);
  void build_triple_constraints(multiple_rl_problem_instances& result_instance, const petri_net_information& pn_info);

  size_t trace_length_{0};
  trace_object_t trace_obj_{};
  continuous_assignment_mapper continuous_mapper_{};
};

}  // namespace celonis::accelerator::operators::process::alignment::rl_align
