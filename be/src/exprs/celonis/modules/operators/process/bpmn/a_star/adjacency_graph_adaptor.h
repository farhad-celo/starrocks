#pragma once

#include "legacy_embedded_ctl/assert.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/replay_types.h"
#include "modules/operators/process/bpmn/replay_utils.h"

namespace celonis::accelerator::operators::process::bpmn::a_star {

/*
 * Adaptor for an bpmn_graph such that it can be used in the a_star implementation. This adaptor only allows for
 * gateways and the target task vertex to be fired. The returned weight for a transition is always one. Given that our
 * heuristic is fixed to 0, this means we are searching for a path which minimizes the total number of transitions.
 */
class adjacency_graph_adaptor {
 public:
  using marking_type = bpmn::marking_with_num_fired_tasks;
  using transition_type = bpmn::transition;
  using transition_list_type = std::vector<transition_type>;

  explicit adjacency_graph_adaptor(const bpmn_graph& model, vertex_id_type target) noexcept
      : model_{model}, target_vertex_{target} {
    const auto target_type{model_.get_vertex(target_vertex_).get_vertex_type()};
    legacy_embedded_debug_assert(is_task(target_type) || is_end(target_type));
  }

  [[nodiscard]] transition_list_type get_enabled_transitions(const marking_type& marking_with_num_fired_tasks) const {
    auto all_enabled_transitions{bpmn::get_enabled_transitions(model_, marking_with_num_fired_tasks.marking())};

    // Remove all transitions on task vertices other than our target, as we are only interested in firing gateways
    // and our target vertex
    all_enabled_transitions.erase(std::remove_if(all_enabled_transitions.begin(), all_enabled_transitions.end(),
                                                 [this](const auto& transition) {
                                                   const auto id{transition.vertex_id()};
                                                   return bpmn::is_task(model_.get_vertex(id)) && id != target_vertex_;
                                                 }),
                                  all_enabled_transitions.end());

    return all_enabled_transitions;
  }

  [[nodiscard]] marking_type fire(const marking_type& marking_with_num_fired_tasks,
                                  const transition_type& transition) const {
    auto number_of_fired_tasks{marking_with_num_fired_tasks.num_fired_tasks()};
    if (bpmn::is_task(model_.get_vertex(transition.vertex_id()))) {
      number_of_fired_tasks++;
      legacy_embedded_debug_assert(number_of_fired_tasks == 1,
                   "Multiple tasks ([{}]) were fired during a single iteration of the search.", number_of_fired_tasks);
    }
    return {bpmn::fire(marking_with_num_fired_tasks.marking(), transition), number_of_fired_tasks};
  }

  [[nodiscard]] marking_type fire_inverse(const marking_type& marking_with_num_fired_tasks,
                                          const transition_type& transition) const {
    auto number_of_fired_tasks{marking_with_num_fired_tasks.num_fired_tasks()};
    if (bpmn::is_task(model_.get_vertex(transition.vertex_id()))) {
      number_of_fired_tasks--;
      legacy_embedded_debug_assert(number_of_fired_tasks == 0,
                   "Multiple tasks ([{}]) were fired during a single iteration of the search.", number_of_fired_tasks);
    }
    return {bpmn::fire_inverse(marking_with_num_fired_tasks.marking(), transition), number_of_fired_tasks};
  }

 private:
  const bpmn_graph& model_;
  const vertex_id_type target_vertex_;
};

}  // namespace celonis::accelerator::operators::process::bpmn::a_star