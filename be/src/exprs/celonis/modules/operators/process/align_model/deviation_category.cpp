#include "deviation_category.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include <oneapi/tbb/parallel_for.h>

#include <cpml/model/bpmn/vertex_types.h>
#include <ctl/algorithm.h>
#include <ctl/assert.h>
#include <ctl/static_array_fwd.h>
#include <ctl/utils/allocation_messages.h>

#include "modules/common/enum_indexed_array.h"
#include "modules/common/execution_context.h"
#include "modules/operators/process/align_model/align_model_types.h"

namespace celonis::accelerator::operators::process::align_model {

namespace {

template <typename T>
using move_type_array_t = common::enum_indexed_array<alignment_move_type, T>;

using move_counts_t = std::unordered_map<cpml::model::bpmn::vertex_id_type, move_type_array_t<size_t>>;

/// Information collected in a single pass over an alignment that is sufficient to derive both the per-move deviation
/// category and the case-level "incomplete" classification.
class alignment_move_stats {
  template <typename T>
  using move_type_array_t = common::enum_indexed_array<alignment_move_type, T>;

  // Each activity has exactly one BPMN vertex_id on the model, so we map from (vertex_id, move_type) -> count.
  // Since there are only five move types the second dimension is a fixed-size array.
  using move_counts_t = std::unordered_map<cpml::model::bpmn::vertex_id_type, move_type_array_t<size_t>>;

 public:
  alignment_move_stats(const alignment_t& alignment) {
    for (size_t idx{0}; idx < alignment.size(); ++idx) {
      const auto& move{alignment.at(idx)};
      // We do not need the counts for unmapped and gateway moves
      if (move.is_unmapped_move() || move.is_gateway_move()) {
        continue;
      }
      debug_assert(move.move_type() == alignment_move_type::SYNC_MOVE ||
                       move.move_type() == alignment_move_type::MODEL_MOVE ||
                       move.move_type() == alignment_move_type::LOG_MOVE,
                   "We are only counting these move types");
      debug_assert(move.move_on_model().has_value(),
                   "The move type invariant guarantees that the above move types all have move_on_model() set.");
      add_or_increment(move.move_on_model().value(), move.move_type(), idx);
    }
  }
  /// A case is incomplete iff its alignment has no LOG_MOVE, it has at least one model move, and all (if any) SYNC
  /// moves precede all MODEL moves.
  /// Pure model/gateway/unmapped alignments (no sync, no log) are considered incomplete too
  [[nodiscard]] bool is_incomplete() const {
    if (total_log_moves_ == 0 && last_model_idx_.has_value()) {
      return !last_sync_idx_.has_value() || (*last_sync_idx_ < *last_model_idx_);
    }
    return false;
  }

  [[nodiscard]] const move_counts_t& get_counts() const { return counts_; }
  [[nodiscard]] std::optional<size_t> get_last_sync_idx() const { return last_sync_idx_; }

 private:
  /**
   * @brief Counts the move of the given @param type for @param vertex_id and updates positional/aggregate counters
   * using the move's position @param idx in the alignment.
   */
  void add_or_increment(cpml::model::bpmn::vertex_id_type vertex_id, alignment_move_type type, size_t idx) {
    if (!counts_.contains(vertex_id)) {
      // If the array does not exist yet we default initialize it
      counts_.emplace(vertex_id, move_type_array_t<size_t>{});
    }
    counts_.at(vertex_id).at(type)++;

    switch (type) {
      case alignment_move_type::SYNC_MOVE:
        last_sync_idx_ = idx;
        break;
      case alignment_move_type::MODEL_MOVE:
        last_model_idx_ = idx;
        break;
      case alignment_move_type::LOG_MOVE:
        ++total_log_moves_;
        break;
      default:
        break;
    }
  }

  move_counts_t counts_{};
  size_t total_log_moves_{0};
  std::optional<size_t> last_sync_idx_{std::nullopt};
  std::optional<size_t> last_model_idx_{std::nullopt};
};

struct move_to_deviation_category_v1_fn {
  deviation_category operator()(const auto& move) {
    switch (move.move_type()) {
      case alignment_move_type::SYNC_MOVE:
        [[fallthrough]];
      case alignment_move_type::GATEWAY_MOVE:
        return deviation_category::CONFORMING;
      case alignment_move_type::MODEL_MOVE:
        debug_assert(move.move_on_model().has_value(),
                     "The move type invariant guarantees that model moves all have move_on_model() set.");
        if (counts.at(move.move_on_model().value()).at(alignment_move_type::LOG_MOVE) == 0) {
          return deviation_category::MISSING;
        }
        return deviation_category::OUT_OF_SEQUENCE;
      case alignment_move_type::LOG_MOVE:
        debug_assert(move.move_on_model().has_value(),
                     "The move type invariant guarantees that log moves all have move_on_model() set.");
        if (counts.at(move.move_on_model().value()).at(alignment_move_type::MODEL_MOVE) > 0) {
          return deviation_category::OUT_OF_SEQUENCE;
        }
        if (counts.at(move.move_on_model().value()).at(alignment_move_type::SYNC_MOVE) == 0) {
          return deviation_category::UNDESIRED;
        }
        return deviation_category::EXCESSIVE;
      case alignment_move_type::UNMAPPED_MOVE:
        return deviation_category::UNMAPPED;
      default:
        ctl::assert_unreachable();
        return deviation_category::CONFORMING;  // To avoid gcc "reached end of non-void function" warning
    }
  }
  const move_counts_t& counts;
};

struct move_to_deviation_category_v2_fn {
  [[nodiscard]] deviation_category operator()(const auto& move, size_t idx) const {
    const auto get_counts_when_model_move{
        [&stats = std::as_const(stats), &move = std::as_const(move)](const alignment_move_type move_type) {
          debug_assert(move_type == alignment_move_type::MODEL_MOVE || move_type == alignment_move_type::LOG_MOVE ||
                           move_type == alignment_move_type::SYNC_MOVE,
                       "Invalid move type for get_counts_when_model_move.");
          debug_assert(move.move_on_model().has_value(),
                       "The move type invariant guarantees that log moves all have move_on_model() set.");
          return stats.get_counts().at(move.move_on_model().value()).at(move_type);
        }};
    switch (move.move_type()) {
      case alignment_move_type::SYNC_MOVE:
        [[fallthrough]];
      case alignment_move_type::GATEWAY_MOVE:
        return deviation_category::CONFORMING;
      case alignment_move_type::MODEL_MOVE:
        if (get_counts_when_model_move(alignment_move_type::LOG_MOVE) == 0) {
          // INCOMPLETE is an internal specialization of MISSING used to drive the L1_INCOMPLETE_VIOLATION edge table.
          bool map_to_incomplete{stats.is_incomplete() && (stats.get_last_sync_idx().value_or(0) <= idx)};
          return map_to_incomplete ? deviation_category::INCOMPLETE : deviation_category::MISSING;
        }
        return deviation_category::OUT_OF_SEQUENCE;
      case alignment_move_type::LOG_MOVE:
        debug_assert(move.move_on_model().has_value(),
                     "The move type invariant guarantees that log moves all have move_on_model() set.");
        if (stats.get_counts().at(move.move_on_model().value()).at(alignment_move_type::MODEL_MOVE) > 0) {
          return deviation_category::OUT_OF_SEQUENCE;
        }
        if (stats.get_counts().at(move.move_on_model().value()).at(alignment_move_type::SYNC_MOVE) == 0) {
          return deviation_category::UNDESIRED;
        }
        return deviation_category::EXCESSIVE;
      case alignment_move_type::UNMAPPED_MOVE:
        return deviation_category::UNMAPPED;
      default:
        ctl::assert_unreachable();
    }
    ctl::assert_unreachable();
  }
  const alignment_move_stats& stats;
};
}  // namespace

template <compute_incomplete_category ALSO_COMPUTE_V2 = compute_incomplete_category::NO>
std::conditional_t<ALSO_COMPUTE_V2 == compute_incomplete_category::YES, deviation_categories,
                   deviation_categories_for_cases_t>
compute_categories(const alignments_t& alignments, const common::execution_context& context) {
  const auto init_result{
      [alignment_size =
           alignments.size()]() -> std::conditional_t<ALSO_COMPUTE_V2 == compute_incomplete_category::YES,
                                                      deviation_categories, deviation_categories_for_cases_t> {
        if constexpr (ALSO_COMPUTE_V2 == compute_incomplete_category::YES) {
          return deviation_categories{ctl::make_static_array_for_overwrite<ctl::static_array<deviation_category>>(
                                          alignment_size, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG)),
                                      ctl::make_static_array_for_overwrite<ctl::static_array<deviation_category>>(
                                          alignment_size, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};
        } else {
          return ctl::make_static_array_for_overwrite<ctl::static_array<deviation_category>>(
              alignment_size, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));
        }
        // throw 1;
      }};
  auto result{init_result()};

  // The category computation is local per variant-alignment, so we can parallelize over them
  tbb::parallel_for(
      tbb::blocked_range<size_t>{0, alignments.size()}, [&result = result, &alignments = std::as_const(alignments),
                                                         &context = std::as_const(context)](const auto& range) {
        const auto get_deviation_category_v1{[](auto& result) -> deviation_categories_for_cases_t& {
          if constexpr (ALSO_COMPUTE_V2 == compute_incomplete_category::YES) {
            return result.deviation_categories_v1;
          } else {
            return result;
          }
        }};

        deviation_categories_for_cases_t& deviation_categories_v1{get_deviation_category_v1(result)};

        for (size_t idx_alignment{range.begin()}; idx_alignment < range.end(); ++idx_alignment) {
          debug_assert(deviation_categories_v1.at(idx_alignment).empty(),
                       "Every alignment should only be processes once by a single thread.");
          if (alignments.at(idx_alignment).has_value()) {
            const alignment_t alignment{alignments.at(idx_alignment).value()};
            const alignment_move_stats alignment_stats{alignment};
            deviation_categories_v1.at(idx_alignment) = ctl::make_static_array_for_overwrite<deviation_category>(
                alignment.size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));
            // Iterate over moves in alignment
            std::ranges::transform(alignment, deviation_categories_v1.at(idx_alignment).begin(),
                                   move_to_deviation_category_v1_fn{alignment_stats.get_counts()});

            if constexpr (ALSO_COMPUTE_V2 == compute_incomplete_category::YES) {
              result.deviation_categories_v2.at(idx_alignment) =
                  ctl::make_static_array_for_overwrite<deviation_category>(alignment.size(),
                                                                           ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));
              const auto indices{std::views::iota(size_t{0}, alignment.size())};
              std::ranges::transform(alignment, indices, result.deviation_categories_v2.at(idx_alignment).begin(),
                                     move_to_deviation_category_v2_fn{alignment_stats});
            }
          }
        }
      });

  return result;
}

template deviation_categories compute_categories<compute_incomplete_category::YES>(
    const alignments_t& alignments, const common::execution_context& context);

template deviation_categories_for_cases_t compute_categories<compute_incomplete_category::NO>(
    const alignments_t& alignments, const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::align_model
