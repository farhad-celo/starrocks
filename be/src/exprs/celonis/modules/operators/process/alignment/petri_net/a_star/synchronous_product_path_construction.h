#pragma once

#include <algorithm>
#include <map>
#include <queue>
#include <vector>

#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/operators/process/alignment/petri_net/a_star/consistent_path_construction.h"
#include "modules/operators/process/alignment/petri_net/a_star/synchronous_product.h"
#include "synchronous_product_heuristic.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

struct synchronous_product_path_construction
    : public a_star::consistent_path_construction<synchronous_product::marking_type,
                                                  synchronous_product::transition_type,
                                                  synchronous_product_heuristic::cost_type> {
  using marking_type = synchronous_product::marking_type;

  explicit synchronous_product_path_construction(const common::execution_context& context)
      : a_star::consistent_path_construction<synchronous_product::marking_type, synchronous_product::transition_type,
                                             synchronous_product_heuristic::cost_type>{context} {}

  // If we have successive model and log moves, all combinations of these (e.g., LLM, LML, and MLL) are equivalent.
  // We only need to explore one of these paths. Here, we use the convention that we only explore paths where all
  // consecutive log moves come before all consecutive model moves.
  // In other words, filter out paths with a model move followed by a log move.
  [[nodiscard]] auto get_filter(const marking_type& marking) const {
    // look up transition that got us here
    const auto it{get_partial_paths().find(marking)};
    auto do_filter_log_transitions{false};
    if (it == end(get_partial_paths())) {
      log::jdebug("A* search on synchronous product: Trying to create filter from previously unseen marking");
    } else {
      do_filter_log_transitions = it->second.is_model_move();
    }
    return [do_filter_log_transitions](const auto& transition) {
      return do_filter_log_transitions && transition.is_log_move();
    };
  }
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
