#include "deviation_category.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <oneapi/tbb/parallel_for.h>

#include <cpml/model/bpmn/vertex_types.h>
#include <ctl/algorithm.h>
#include <ctl/assert.h>
#include <ctl/static_array_fwd.h>
#include <ctl/utils/allocation_messages.h>

#include "modules/common/enum_indexed_array.h"
#include "modules/operators/process/align_model/align_model_types.h"

namespace celonis::accelerator::operators::process::align_model {

namespace {

template <typename T>
using move_type_array_t = common::enum_indexed_array<alignment_move_type, T>;

using move_counts_t = std::unordered_map<cpml::model::bpmn::vertex_id_type, move_type_array_t<size_t>>;

/**
 * @brief We count the number of moves per type and activity for SYNC, LOG and MODEL, each activity has exactly once
 * BPMN vertex_id on the model.
 * Therefore we map from (vertex_id,move_type) -> count
 * Since we know that there are only five move types the second dimension can just be an array instead of using two
 * maps or a composite key for a single map
 */
[[nodiscard]] move_counts_t count_moves(const alignment_t& alignment) {
  move_counts_t counts{};

  const auto add_or_increment{[&counts](cpml::model::bpmn::vertex_id_type vertex_id, alignment_move_type type) {
    if (!counts.contains(vertex_id)) {
      // If the array does not exist yet we default initialize it
      counts.emplace(vertex_id, move_type_array_t<size_t>{});
    }
    counts.at(vertex_id).at(type)++;
  }};
  for (const auto& move : alignment) {
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
    add_or_increment(move.move_on_model().value(), move.move_type());
  }
  return counts;
}

struct move_to_deviation_category_fn {
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

}  // namespace

deviation_categories_for_cases_t compute_categories(const alignments_t& alignments) {
  auto result{ctl::make_static_array_for_overwrite<ctl::static_array<deviation_category>>(
      alignments.size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};
  // The category computation is local per variant-alignment, so we can parallelize over them
  tbb::parallel_for(tbb::blocked_range<size_t>{0, result.size()}, [&result, &alignments = std::as_const(alignments)](
                                                                      const auto& range) {
    // Iterate over alignments in block
    for (size_t idx_alignment{range.begin()}; idx_alignment < range.end(); ++idx_alignment) {
      debug_assert(result.at(idx_alignment).empty(),
                   "Every alignment should only be processes once by a single thread.");
      if (alignments.at(idx_alignment).has_value()) {
        const alignment_t& alignment{alignments.at(idx_alignment).value()};
        const auto counts{count_moves(alignment)};
        result.at(idx_alignment) = ctl::make_static_array_for_overwrite<deviation_category>(
            alignment.size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG));
        // Iterate over moves in alignment
        std::ranges::transform(alignment, result.at(idx_alignment).begin(), move_to_deviation_category_fn{counts});
      }
    }
  });

  return result;
}
}  // namespace celonis::accelerator::operators::process::align_model