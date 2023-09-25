#include "modules/operators/process/alignment/petri_net/murata/murata_reductions.h"

#include <optional>

#include <boost/graph/adjacency_list.hpp>

#include "modules/common/exceptions.h"
#include "modules/operators/process/alignment/petri_net/murata/fusion_series_places.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/alignment/petri_net/petri_net_builder.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

namespace {

std::optional<fusion_of_series_places> find_reduction_to_apply(
    const petri_net_builder& pn_builder, const std::unordered_set<std::string>& keep_transitions) {
  for (const auto& node_desc : boost::make_iterator_range(boost::vertices(pn_builder.graph()))) {
    // If needed, one can optimize the reductions by returning all rules that apply
    const auto& node_id{pn_builder.graph()[node_desc].id()};
    if (auto fsp_rule{fusion_of_series_places::try_build(pn_builder, keep_transitions, node_id)}; fsp_rule) {
      return fsp_rule;
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
  auto prev_vertex_count{std::numeric_limits<size_t>::max()};
  while (prev_vertex_count > boost::num_vertices(reduced_builder.graph())) {
    prev_vertex_count = boost::num_vertices(reduced_builder.graph());

    auto rule{find_reduction_to_apply(reduced_builder, keep_transitions)};
    if (rule) {
      rule->apply(reduced_builder);
    }
  }

  return reduced_builder.to_petri_net_representation();
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
