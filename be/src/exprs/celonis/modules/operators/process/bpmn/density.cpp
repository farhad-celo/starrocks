#include "density.h"

#include "modules/common/exceptions.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"

namespace celonis::accelerator::operators::process::bpmn {

cel_float_t calculate_density(const bpmn_graph& bpmn_graph) {
  if (!bpmn_graph.is_single_object()) {
    throw common::cpm_exception{"Single object BPMN required for density calculation"};
  }
  constexpr size_t num_start_vertices{1};
  constexpr size_t num_end_vertices{1};

  size_t num_tasks{0};
  size_t num_gateways{0};

  for (const auto& [_, v] : bpmn_graph.get_vertices()) {
    std::visit(
        legacy_embedded_ctl::overloaded{[](const auto& /*vertex_type*/) {}, [&num_tasks](const task& /*unused*/) { num_tasks++; },
                        [&num_gateways](const parallel& /*unused*/) { num_gateways++; },
                        [&num_gateways](const exclusive_choice& /*unused*/) { num_gateways++; }},
        v.get_vertex_type());
  }

  cel_float_t max_edges{};
  if (num_gateways == 0) {
    if (num_tasks == 0) {
      // A model without tasks and gateways can have a single edge from start to end
      max_edges = 1.0;
    } else {
      // A model without gateways has to be a sequence of tasks
      max_edges = 2.0 * static_cast<cel_float_t>(num_tasks) + 1.0;
    }
  } else {
    max_edges = static_cast<cel_float_t>(
        3 * num_tasks +                      // Tasks have at most one ingoing, one outgoing and one self-loop
        num_gateways * (num_gateways - 1) +  // Each gateway can connect to all others, but can't have self-loops
        num_start_vertices +                 // There is exactly one outgoing edge from each start
        num_end_vertices                     // There is exactly one ingoing edge to each end
    );
  }
  return static_cast<cel_float_t>(bpmn_graph.get_edges().size()) / max_edges;
}
}  // namespace celonis::accelerator::operators::process::bpmn