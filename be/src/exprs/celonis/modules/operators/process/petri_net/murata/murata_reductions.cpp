#include "modules/operators/process/petri_net/murata/murata_reductions.h"

#include <algorithm>
#include <optional>
#include <variant>

#include "legacy_embedded_ctl/utility.h"
#include "modules/common/exceptions.h"
#include "modules/operators/process/petri_net/murata/fusion_series_places.h"
#include "modules/operators/process/petri_net/murata/fusion_series_transitions.h"
#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

namespace {

using reduction_rule_t =
    std::variant<fusion_series_places, fusion_series_transition_and_tau, fusion_series_tau_and_keep_transition>;

std::optional<reduction_rule_t> find_reduction_to_apply(const petri_net_builder& pn,
                                                        const std::unordered_set<std::string>& keep_transitions) {
  // To make the reductions deterministic, otherwise it's impossible to test
  //  This should not be a performance bottleneck, but can be optimized if needed
  const auto sorted_nodes{[&pn = std::as_const(pn)]() {
    std::vector<petri_net_builder::node_id_type> sorted_nodes{};
    for (const auto& [node, _] : pn.get_nodes()) {
      sorted_nodes.emplace_back(node);
    }
    std::ranges::sort(sorted_nodes);
    return sorted_nodes;
  }()};

  for (const auto& node : sorted_nodes) {
    // If needed, one can optimize the reductions by returning all rules that apply
    if (auto fsp_rule{fusion_series_places::try_build(pn, keep_transitions, node)}; fsp_rule) {
      return fsp_rule;
    }
    if (auto fst_transition_tau{fusion_series_transition_and_tau::try_build(pn, keep_transitions, node)};
        fst_transition_tau) {
      return fst_transition_tau;
    }
    if (auto fst_tau_keep{fusion_series_tau_and_keep_transition::try_build(pn, keep_transitions, node)}; fst_tau_keep) {
      return fst_tau_keep;
    }
  }
  return std::nullopt;
}

}  // namespace

petri_net_representation reduce_murata(const petri_net_representation& pn_repr,
                                       const std::unordered_set<std::string>& keep_transitions) {
  static_assert(std::is_same_v<std::string, petri_net_builder::node_id_type>);
  petri_net_builder reduced_builder{pn_repr};

  // INB4: It's safer to break the loop when there are no changes
  //   because a bug might cause a rule to be enabled and perform a no-op
  auto prev_node_count{std::numeric_limits<size_t>::max()};
  while (prev_node_count > reduced_builder.node_count()) {
    prev_node_count = reduced_builder.node_count();

    auto optional_rule{find_reduction_to_apply(reduced_builder, keep_transitions)};
    if (optional_rule) {
      std::visit(legacy_embedded_ctl::overloaded{[&reduced_builder](const auto& rule) { rule.apply(reduced_builder); }},
                 optional_rule.value());
    }
  }

  return reduced_builder.to_petri_net_representation();
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
