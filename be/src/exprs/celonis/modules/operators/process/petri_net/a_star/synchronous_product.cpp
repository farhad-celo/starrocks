#include "synchronous_product.h"

#include <algorithm>

#include "modules/common/exceptions.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

synchronous_product::synchronous_product(const petri_net_accessor& petri_net, std::span<const row_id> variant)
    : petri_net_{petri_net}, variant_length_{variant.size()} {
  // fill cross transitions
  for (variant_transition_type index{}; index != variant.size(); ++index) {
    const auto label{variant[index]};
    // find all transitions in petri net with this label
    for (auto transition : petri_net_.get_transitions_for_label(label)) {
      synchronous_transitions_.emplace_back(index, transition);
    }
  }
}

synchronous_product::transition_list_type synchronous_product::get_enabled_transitions(
    const synchronous_product::marking_type& marking) const {
  auto result{get_model_moves(marking)};
  // add log move
  const auto log_move{get_log_move(marking)};
  if (log_move.has_value()) {
    result.emplace_back(log_move.value());
  }
  for (const auto& synchronous_move : get_synchronous_moves(marking)) {
    result.emplace_back(synchronous_move);
  }
  return result;
}

synchronous_product::marking_type synchronous_product::fire(
    synchronous_product::marking_type marking, const synchronous_product::transition_type& transition) const {
  if (transition.move_on_log) {
    if (marking.variant_place >= variant_length_) {
      throw common::internal_exception{"{}: A* search: Can't fire log move on final log place",
                                       petri_net_.get_user_visible_operator_name()};
    }
    marking.variant_place += 1;
  }
  if (transition.petri_net_transition.has_value()) {
    petri_net_.fire_no_alloc(marking.petri_net_marking, transition.petri_net_transition.value());
  }
  return marking;
}

synchronous_product::marking_type synchronous_product::fire_inverse(
    synchronous_product::marking_type marking, const synchronous_product::transition_type& transition) const {
  if (transition.move_on_log) {
    if (marking.variant_place < 1) {
      throw common::internal_exception{"{}: A* search: Can't reverse log move on initial log place",
                                       petri_net_.get_user_visible_operator_name()};
    }
    marking.variant_place -= 1;
  }
  if (transition.petri_net_transition.has_value()) {
    petri_net_.fire_inverse_no_alloc(marking.petri_net_marking, transition.petri_net_transition.value());
  }
  return marking;
}

synchronous_product::transition_list_type synchronous_product::get_synchronous_moves(
    const synchronous_product::marking_type& marking) const {
  transition_list_type result{};
  for (const auto& [variant_index, petri_net_transition] : synchronous_transitions_) {
    if (variant_index == marking.variant_place &&
        petri_net_.is_transition_enabled(marking.petri_net_marking, petri_net_transition)) {
      result.emplace_back(transition_type{true, true, petri_net_transition});
    }
  }
  return result;
}

std::optional<synchronous_product::transition_type> synchronous_product::get_log_move(
    const synchronous_product::marking_type& marking) const {
  return marking.variant_place < variant_length_ ? std::optional{transition_type{true, false, {}}}
                                                 : std::optional<transition_type>{};
}

synchronous_product::transition_list_type synchronous_product::get_model_moves(
    const synchronous_product::marking_type& marking) const {
  const auto transitions{petri_net_.get_enabled_transitions(marking.petri_net_marking)};
  transition_list_type result{};
  result.reserve(transitions.size());
  for (const auto& transition : transitions) {
    const auto is_visible{!string_to_int_mapper::is_tau_transition(petri_net_.get_label(transition))};
    result.emplace_back(transition_type{false, is_visible, transition});
  }
  return result;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
