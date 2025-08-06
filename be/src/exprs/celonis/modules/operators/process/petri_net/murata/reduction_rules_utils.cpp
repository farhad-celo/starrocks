#include "reduction_rules_utils.h"

#include "modules/operators/process/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

namespace {

bool is_single_entry_single_exit_node(const petri_net_builder& pn,
                                      const petri_net_builder_node::node_id_type& node_id) {
  if (!pn.is_node(node_id)) {
    return false;
  }
  return pn.in_degree(node_id) == 1 && pn.out_degree(node_id) == 1;
}

}  // namespace

// If the node is Single-Entry Single-Exit with the given weight, then returns the input and output nodes
std::optional<std::pair<petri_net_builder::node_id_type, petri_net_builder::node_id_type>>
get_if_single_entry_single_exit_with_weight(const petri_net_builder& pn,
                                            const petri_net_builder_node::node_id_type& node_id,
                                            petri_net_builder::arc_weight_type weight) {
  if (!is_single_entry_single_exit_node(pn, node_id)) {
    return std::nullopt;
  }
  const auto pre_node{*std::begin(pn.pre_set(node_id))};
  const auto post_node{*std::begin(pn.post_set(node_id))};

  const petri_net_builder::arc_weight_type in_weight{pn.at_arc(pre_node, node_id)};
  const petri_net_builder::arc_weight_type out_weight{pn.at_arc(node_id, post_node)};

  if (in_weight == weight && out_weight == weight) {
    return std::make_pair(pre_node, post_node);
  }
  return std::nullopt;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
