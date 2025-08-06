#include "align_synchronous_product.h"

#include "modules/operators/process/alignment/optimal/synchronous_product_heuristic.h"
#include "modules/operators/process/alignment/optimal/synchronous_product_path_construction.h"
#include "modules/operators/process/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/petri_net/a_star/synchronous_product.h"
#include "modules/operators/process/petri_net/petri_net.h"

namespace celonis::accelerator::operators::process::alignment::optimal {

trace_alignment_t search_synchronous_product_for_optimal_alignment(const petri_net::petri_net_accessor& accessor,
                                                                   std::span<const row_id> variant, int iterations,
                                                                   const common::execution_context& context) {
  petri_net::a_star::synchronous_product synchronous_petri_net{accessor, variant};
  auto result{petri_net::a_star::a_star_search(synchronous_petri_net, synchronous_product_heuristic{variant, accessor},
                                               synchronous_product_path_construction{context},
                                               synchronous_petri_net.get_initial_marking(), 100, iterations)};
  using transitions_type = petri_net::a_star::synchronous_product::transition_list_type;
  if (!std::holds_alternative<transitions_type>(result)) {
    return {};
  }
  const auto& value = std::get<transitions_type>(result);
  trace_alignment alignment{};
  alignment.reserve(value.size());

  auto variant_it{std::cbegin(variant)};
  for (const auto& transition : value) {
    if (transition.is_log_move()) {
      alignment.add(alignment_move::log(*variant_it));
      ++variant_it;
    } else if (transition.is_synchronous_move()) {
      alignment.add(alignment_move::sync(*variant_it, *transition.petri_net_transition));
      ++variant_it;
    } else if (transition.is_model_move()) {
      const auto label{accessor.get_label(*transition.petri_net_transition)};
      alignment.add(alignment_move::model(label, *transition.petri_net_transition));
    }
  }
  return std::optional{alignment};
}

}  // namespace celonis::accelerator::operators::process::alignment::optimal
