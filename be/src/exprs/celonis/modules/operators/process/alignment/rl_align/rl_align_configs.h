#pragma once

#include <algorithm>

#include <boost/functional/hash.hpp>

#include "legacy_embedded_ctl/assert.h"
#include "modules/common/exceptions.h"
#include "modules/common/int_types.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

using rl_weight_t = float;

/**
 * The reconnected unfolding is constructed by reconnecting cutoff events.
 *  Cutoffs are either due to a branching or to a loop
 *  One needs both BPs (True, True) and (True, False)
 */
struct unfolding_config {
  bool add_if_cutoffs{false};
  bool add_loop_cutoffs{false};
};

struct solver_config {
  /// Initial Bias for NULL labeling
  rl_weight_t null_label_bias{0.0};
  /// Maximum iterations of algorithm
  uint64_t max_iterations{0};
  /// Range to clip supports
  rl_weight_t supports_range{0.0};
  /// If change < min_delta, stop iterations
  rl_weight_t min_delta{0.0};
  /// Limit the completion step's BFS runs to avoid it running infinitely
  int bfs_max_iterations{0};
  int bfs_max_insertions{0};
};

enum class compatibility_type : int8_t { RIGHT_ORDER, WRONG_ORDER, EXCLUSIVE, PARALLEL, DELETION };

/** Config for creating the constraints (weights of RL problem) */
struct constraints_config {
  static constexpr size_t NUMBER_OF_VARIABLES{5};

  static constexpr size_t RIGHT_ORDER_COMPATIBILITY_OFFSET{0};
  static constexpr size_t WRONG_ORDER_COMPATIBILITY_OFFSET{1};
  static constexpr size_t EXCLUSIVE_COMPATIBILITY_OFFSET{2};
  static constexpr size_t PARALLEL_COMPATIBILITY_OFFSET{3};
  static constexpr size_t DELETION_COMPATIBILITY_OFFSET{4};

  /// Right Order
  rl_weight_t right_order_compatibility{0.0};
  /// Wrong Order
  rl_weight_t wrong_order_compatibility{0.0};
  /// Exclusive
  rl_weight_t exclusive_compatibility{0.0};
  /// Parallel/Loop compatibility values
  rl_weight_t parallel_compatibility{0.0};
  /// Deletion constraint (for triple constraints)
  rl_weight_t deletion_compatibility{0.0};

  [[nodiscard]] static size_t get_offset_for_compatibility_type(compatibility_type compat_type) {
    switch (compat_type) {
      using enum compatibility_type;
      case RIGHT_ORDER:
        return RIGHT_ORDER_COMPATIBILITY_OFFSET;
      case WRONG_ORDER:
        return WRONG_ORDER_COMPATIBILITY_OFFSET;
      case EXCLUSIVE:
        return EXCLUSIVE_COMPATIBILITY_OFFSET;
      case PARALLEL:
        return PARALLEL_COMPATIBILITY_OFFSET;
      case DELETION:
        return DELETION_COMPATIBILITY_OFFSET;
      default:
        throw common::internal_exception{"ALIGN: Unknown compatibility type."};
    }
  }

  constexpr bool operator==(const constraints_config& rhs) const noexcept = default;

  constexpr std::partial_ordering operator<=>(const constraints_config& rhs) const noexcept {
    if (right_order_compatibility != rhs.right_order_compatibility) {
      return right_order_compatibility <=> rhs.right_order_compatibility;
    }
    if (wrong_order_compatibility != rhs.wrong_order_compatibility) {
      return wrong_order_compatibility <=> rhs.wrong_order_compatibility;
    }
    if (exclusive_compatibility != rhs.exclusive_compatibility) {
      return exclusive_compatibility <=> rhs.exclusive_compatibility;
    }
    if (parallel_compatibility != rhs.parallel_compatibility) {
      return parallel_compatibility <=> rhs.parallel_compatibility;
    }
    return deletion_compatibility <=> rhs.deletion_compatibility;
  }
};

struct rl_problem_behavioral_overview {
  bool has_right_order_constraint{false};
  bool has_wrong_order_constraint{false};
  bool has_exclusive_constraint{false};
  bool has_parallel_constraint{false};
  bool has_deletion_constraint{false};
};

struct constraints_behavioral_hash final {
  const rl_problem_behavioral_overview& behavioral_overview;
  constexpr size_t operator()(const constraints_config& c) const noexcept {
    size_t seed{42};
    if (behavioral_overview.has_right_order_constraint) {
      boost::hash_combine(seed, c.right_order_compatibility);
    }
    if (behavioral_overview.has_wrong_order_constraint) {
      boost::hash_combine(seed, c.wrong_order_compatibility);
    }
    if (behavioral_overview.has_exclusive_constraint) {
      boost::hash_combine(seed, c.exclusive_compatibility);
    }
    if (behavioral_overview.has_parallel_constraint) {
      boost::hash_combine(seed, c.parallel_compatibility);
    }
    if (behavioral_overview.has_deletion_constraint) {
      boost::hash_combine(seed, c.deletion_compatibility);
    }
    return seed;
  }
};

struct constraints_equal_to final {
  const rl_problem_behavioral_overview& behavioral_overview;
  constexpr bool operator()(const constraints_config& lhs, const constraints_config& rhs) const noexcept {
    return (!behavioral_overview.has_right_order_constraint ||
            lhs.right_order_compatibility == rhs.right_order_compatibility) &&
           (!behavioral_overview.has_wrong_order_constraint ||
            lhs.wrong_order_compatibility == rhs.wrong_order_compatibility) &&
           (!behavioral_overview.has_exclusive_constraint ||
            lhs.exclusive_compatibility == rhs.exclusive_compatibility) &&
           (!behavioral_overview.has_parallel_constraint || lhs.parallel_compatibility == rhs.parallel_compatibility) &&
           (!behavioral_overview.has_deletion_constraint || lhs.deletion_compatibility == rhs.deletion_compatibility);
  }
};

/**
 * Circular iterator over range of values. When the range is over, it is reset to the beginning.
 *  The range is always kept on a valid state.
 */
class constraints_range {
 public:
  constexpr constraints_range() noexcept = default;
  constexpr constraints_range(const rl_weight_t* start, const rl_weight_t* end) noexcept
      : start_{start}, end_{end}, current_{start} {}

  /// Returns true if range was reset
  [[nodiscard]] constexpr bool advance_to_next() noexcept {
    next();
    if (is_done()) {
      reset();
      return true;
    }
    return false;
  }
  [[nodiscard]] constexpr rl_weight_t operator*() const noexcept { return *current_; }

 private:
  constexpr void next() noexcept { ++current_; }
  constexpr void reset() noexcept { current_ = start_; }
  [[nodiscard]] constexpr bool is_done() const noexcept { return current_ == end_; }

  const rl_weight_t* start_{nullptr};
  const rl_weight_t* end_{nullptr};
  const rl_weight_t* current_{nullptr};
};

struct constraints_config_set {
  std::vector<constraints_config> data;
};

class constraints_config_grid {
  using vector_t = std::vector<rl_weight_t>;

 public:
  constraints_config_grid() = default;

  constraints_config_grid(vector_t right_order_compatibilities, vector_t wrong_order_compatibilities,
                          vector_t exclusive_compatibilities, vector_t parallel_compatibilities,
                          vector_t deletion_compatibilities)
      : right_order_compatibilities_{std::move(right_order_compatibilities)},
        wrong_order_compatibilities_{std::move(wrong_order_compatibilities)},
        exclusive_compatibilities_{std::move(exclusive_compatibilities)},
        parallel_compatibilities_{std::move(parallel_compatibilities)},
        deletion_compatibilities_{std::move(deletion_compatibilities)} {
    // Check that values have the right sign (some constraints need to be negative, some positive)
    //  If users can choose their grid, can provide feedback by using warnings
    [[maybe_unused]] const auto is_not_negative_lmb{
        [](const auto& values) { return std::ranges::all_of(values, [](const auto value) { return value >= 0; }); }};

    [[maybe_unused]] const auto is_not_positive_lmb{
        [](const auto& values) { return std::ranges::all_of(values, [](const auto value) { return value <= 0; }); }};

    legacy_embedded_debug_assert(is_not_negative_lmb(right_order_compatibilities_));
    legacy_embedded_debug_assert(is_not_positive_lmb(wrong_order_compatibilities_));
    legacy_embedded_debug_assert(is_not_positive_lmb(exclusive_compatibilities_));
    legacy_embedded_debug_assert(is_not_negative_lmb(parallel_compatibilities_));
    legacy_embedded_debug_assert(is_not_positive_lmb(deletion_compatibilities_));
  }

  [[nodiscard]] constraints_config_set make_full_grid() const {
    std::vector<constraints_config> constraints;
    for (auto deletion_compatibility : deletion_compatibilities_) {
      for (auto parallel_compatibility : parallel_compatibilities_) {
        for (auto exclusive_compatibility : exclusive_compatibilities_) {
          for (auto wrong_order_compatibility : wrong_order_compatibilities_) {
            for (auto right_order_compatibility : right_order_compatibilities_) {
              constraints.emplace_back(constraints_config{right_order_compatibility, wrong_order_compatibility,
                                                          exclusive_compatibility, parallel_compatibility,
                                                          deletion_compatibility});
            }
          }
        }
      }
    }
    return {constraints};
  }

 private:
  vector_t right_order_compatibilities_{};
  vector_t wrong_order_compatibilities_{};
  vector_t exclusive_compatibilities_{};
  vector_t parallel_compatibilities_{};
  vector_t deletion_compatibilities_{};
};

struct problem_building_config {
  /// Maximum distance for constraints to affect each other
  size_t maximum_distance{0};
};

using lcs_chunk_size_type = size_t;
struct rl_align_config {
  solver_config solver_cfg{};
  constraints_config_grid constraints_cfg_grid{};
  problem_building_config problem_building_cfg{};
  lcs_chunk_size_type lcs_chunk_size{};
};

[[nodiscard]] rl_align_config make_default_rl_align_config();

[[nodiscard]] rl_align_config make_small_rl_align_config();

}  // namespace celonis::accelerator::operators::process::alignment::rl_align

// Explicit full template specialization of std::hash for constraints_config. Makes usage in unordered_map/set easier.
template <>
struct std::hash<celonis::accelerator::operators::process::alignment::rl_align::constraints_config> final {
  size_t operator()(
      const celonis::accelerator::operators::process::alignment::rl_align::constraints_config& c) const noexcept {
    auto seed{std::hash<float>{}(c.right_order_compatibility)};
    boost::hash_combine(seed, c.wrong_order_compatibility);
    boost::hash_combine(seed, c.exclusive_compatibility);
    boost::hash_combine(seed, c.parallel_compatibility);
    boost::hash_combine(seed, c.deletion_compatibility);
    return seed;
  }
};
