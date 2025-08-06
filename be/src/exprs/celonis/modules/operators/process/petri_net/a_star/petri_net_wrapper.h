#pragma once

#include <span>
#include <vector>

#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

struct petri_net_wrapper {
  using transition_type = petri_net_transition_id;
  using marking_type = petri_net::marking_type;
  using transition_list_type = petri_net_accessor::transition_list_type;
  using transition_span_type = petri_net_accessor::transition_span_type;

  explicit constexpr petri_net_wrapper(petri_net_accessor& petri_net) noexcept : petri_net_{petri_net} {}

  [[nodiscard]] transition_span_type get_enabled_transitions(const marking_type& marking) const {
    return petri_net_.get_enabled_transitions(marking);
  }

  [[nodiscard]] marking_type fire(const marking_type& marking, transition_type transition) const {
    return petri_net_.fire(marking, transition);
  }

  [[nodiscard]] marking_type fire_inverse(const marking_type& marking, transition_type transition) const {
    return petri_net_.fire_inverse(marking, transition);
  }

 private:
  petri_net_accessor& petri_net_;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
