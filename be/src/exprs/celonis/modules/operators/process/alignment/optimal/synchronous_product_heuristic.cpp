#include "synchronous_product_heuristic.h"

namespace celonis::accelerator::operators::process::alignment::optimal {

synchronous_product_heuristic::cost_type synchronous_product_heuristic::get_weight(const transition_type& transition) {
  return transition.is_log_move() || transition.is_visible_model_move() ? 1 : 0;
}

std::optional<synchronous_product_heuristic::cost_type> synchronous_product_heuristic::estimate(
    const marking_type& marking) const {
  // TODO(a.swoboda) This can't happen currently, but if there is a loop from a final marking, or a path to another
  //  final marking, than this is wrong
  if (petri_net.is_final_marking(marking.petri_net_marking)) {
    return {trace.size() - marking.variant_place};  // the amount of log moves needed
  }
  if (trace.size() == marking.variant_place) {
    // TODO(a.swoboda) we can do better here by simply using the shortest paths matrix
    return {petri_net.get_silent_enabled_transitions(marking.petri_net_marking).empty() ? 1 : 0};
  }
  // TODO(a.swoboda) The general case is much harder. It might be possible to leverage the shortest paths matrix again,
  //  or we do something like Definition 4.7 [here](https://www.sciencedirect.com/science/article/pii/S0306437920300545)
  return {0};
}

}  // namespace celonis::accelerator::operators::process::alignment::optimal
