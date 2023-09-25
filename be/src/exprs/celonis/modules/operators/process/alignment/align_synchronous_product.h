#pragma once

#include <optional>
#include <vector>

#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

std::optional<trace_alignment> search_synchronous_product_for_optimal_alignment(
    petri_net::petri_net_accessor& accessor, std::span<const row_id> variant, int iterations,
    const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::alignment
