#pragma once

#include <optional>

#include <cpml/model/bpmn_graph.h>
#include <ctl/assert.h>

#include "modules/operators/process/bpmn/replay_types.h"
#include "modules/operators/process/bpmn/replay_utils.h"

namespace celonis::accelerator::operators::process::bpmn::a_star {

/*
 * A heuristic for an adjacency_graph such that it can be used in the a_star implementation. As this heuristic always
 * returns an estimate of 0, we are actually performing Dijkstra's algorithm instead of a_star.
 */
class adjacency_graph_heuristic {
 public:
  using transition_type = adjacency_graph_adaptor::transition_type;
  using marking_type = bpmn::marking_with_num_fired_tasks;
  using cost_type = int32_t;

  adjacency_graph_heuristic(const cpml::model::bpmn_graph& model,
                            cpml::model::bpmn::vertex_id_type target_vertex) noexcept
      : model_{model}, target_vertex_{target_vertex} {
    const auto target_type{model_.get_vertex(target_vertex).get_vertex_type()};
    debug_assert(is_task(target_type) || is_end(target_type));
  };

  [[nodiscard]] static cost_type get_weight(const transition_type& /*transition*/) noexcept { return 1; }
  [[nodiscard]] static std::optional<cost_type> estimate(const marking_type& /*marking*/) noexcept { return 0; }

  [[nodiscard]] bool is_target(const marking_type& marking_with_num_fired_tasks) const {
    if (is_end(model_.get_vertex(target_vertex_))) {
      // The end vertex has no transitions defined and hence is not fired in the search.
      return bpmn::vertex_reached(model_, marking_with_num_fired_tasks.marking(), target_vertex_);
    }
    // Task vertices are fired in the search, so we check if there is a token on any of the outgoing edges.
    // Additionally, a task vertex should be fired to make this work with loops (where the initial tokens are equal
    // to the target tokens).
    return bpmn::vertex_reached_inverse(model_, marking_with_num_fired_tasks.marking(), target_vertex_) &&
           marking_with_num_fired_tasks.num_fired_tasks() > 0;
  }

 private:
  const cpml::model::bpmn_graph& model_;
  const cpml::model::bpmn::vertex_id_type target_vertex_;
};

}  // namespace celonis::accelerator::operators::process::bpmn::a_star