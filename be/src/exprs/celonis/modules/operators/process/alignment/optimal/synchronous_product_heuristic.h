#pragma once

#include <optional>
#include <vector>

#include "modules/operators/process/petri_net/a_star/synchronous_product.h"  // TODO(a.swoboda) consider forward declarations for marking_type
#include "modules/operators/process/petri_net/petri_net.h"

namespace celonis::accelerator::operators::process::alignment::optimal {

struct synchronous_product_heuristic {
  using transition_type = petri_net::a_star::synchronous_product::transition_type;
  using marking_type = petri_net::a_star::synchronous_product::marking_type;
  using cost_type = int32_t;

  [[nodiscard]] static cost_type get_weight(const transition_type& transition);

  [[nodiscard]] std::optional<cost_type> estimate(const marking_type& marking) const;

  [[nodiscard]] bool is_target(const marking_type& marking) const {
    return marking.variant_place == trace.size() && petri_net.is_final_marking(marking.petri_net_marking);
  }

  std::span<const petri_net::petri_net_accessor::label_type> trace;
  const petri_net::petri_net_accessor& petri_net;
};

}  // namespace celonis::accelerator::operators::process::alignment::optimal
