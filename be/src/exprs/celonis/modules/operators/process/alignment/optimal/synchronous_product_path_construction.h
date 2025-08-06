#pragma once

#include <algorithm>
#include <map>
#include <queue>
#include <vector>

#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/operators/process/alignment/optimal/synchronous_product_heuristic.h"
#include "modules/operators/process/petri_net/a_star/consistent_path_construction.h"
#include "modules/operators/process/petri_net/a_star/synchronous_product.h"

namespace celonis::accelerator::operators::process::alignment::optimal {

struct synchronous_product_path_construction
    : public petri_net::a_star::consistent_path_construction<petri_net::a_star::synchronous_product::marking_type,
                                                             petri_net::a_star::synchronous_product::transition_type,
                                                             synchronous_product_heuristic::cost_type> {
  using transition_type = petri_net::a_star::synchronous_product::transition_type;
  using marking_type = petri_net::a_star::synchronous_product::marking_type;

  explicit synchronous_product_path_construction(const common::execution_context& context)
      : petri_net::a_star::consistent_path_construction<marking_type, transition_type,
                                                        synchronous_product_heuristic::cost_type>{context} {}

  // If we have successive model and log moves, all combinations of these (e.g., LLM, LML, and MLL) are equivalent.
  // We only need to explore one of these paths. Here, we use the convention that we only explore paths where all
  // consecutive log moves come before all consecutive model moves.
  // In other words, filter out paths with a model move followed by a log move.
  [[nodiscard]] auto get_filter(const marking_type& marking) const {
    // look up transition that got us here
    const auto it{get_partial_paths().find(marking)};
    auto previously_model_move{false};

    if (it == end(get_partial_paths())) {
      log::jdebug("A* search on synchronous product: Trying to create filter from previously unseen marking");
    } else {
      previously_model_move = it->second.is_model_move();
    }
    return [previously_model_move](const auto& transition) {
      return !(previously_model_move && transition.is_log_move());
    };
  }
};

}  // namespace celonis::accelerator::operators::process::alignment::optimal
