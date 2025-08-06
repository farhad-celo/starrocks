#pragma once

#include <vector>

#include "modules/operators/process/petri_net/petri_net_fwd.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment::optimal {

trace_alignment_t search_synchronous_product_for_optimal_alignment(const petri_net::petri_net_accessor& accessor,
                                                                   std::span<const row_id> variant, int iterations,
                                                                   const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::alignment::optimal
