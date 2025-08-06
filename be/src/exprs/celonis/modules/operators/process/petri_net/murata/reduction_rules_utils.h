#pragma once

#include <optional>
#include <utility>

#include "modules/operators/process/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

// If the node is Single-Entry Single-Exit with the given weight, then returns the input and output nodes
std::optional<std::pair<petri_net_builder::node_id_type, petri_net_builder::node_id_type>>
get_if_single_entry_single_exit_with_weight(const petri_net_builder& pn,
                                            const petri_net_builder_node::node_id_type& node_id,
                                            petri_net_builder::arc_weight_type weight);

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
