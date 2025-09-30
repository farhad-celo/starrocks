#include "replay_utils.h"

#include <algorithm>
#include <functional>
#include <string>

#include <cpml/model/bpmn_graph.h>
#include <ctl/assert.h>
#include <ctl/conversion.h>
#include <ctl/utility.h>

#include "modules/common/exceptions.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

// TODO(n.weber): Temporary using decls until code is migrated to CPML
using cpml::activity_id_t;
using cpml::model::bpmn_graph;
using cpml::model::bpmn::exclusive_choice;
using cpml::model::bpmn::parallel;
using cpml::model::bpmn::start;
using cpml::model::bpmn::task;
using cpml::model::bpmn::vertex_id_type;
using cpml::model::bpmn::vertex_type;

[[nodiscard]] transition compute_single_transition(const vertex_id_type vertex_id,
                                                   const std::vector<vertex_id_type>& ingoing_vertices,
                                                   const std::vector<vertex_id_type>& outgoing_vertices) {
  tokens_t consumed{};
  consumed.reserve(ingoing_vertices.size());
  tokens_t produced{};
  produced.reserve(outgoing_vertices.size());

  // Create a consumed token for every ingoing edge
  for (const auto& ingoing_id : ingoing_vertices) {
    consumed.emplace_back(ingoing_id, vertex_id);
  }

  // Create a produced token for every outgoing edge
  for (const auto& outgoing_id : outgoing_vertices) {
    produced.emplace_back(vertex_id, outgoing_id);
  }

  return {vertex_id, std::move(consumed), std::move(produced)};
}

[[nodiscard]] transitions_t compute_product_transitions(const vertex_id_type vertex_id,
                                                        const std::vector<vertex_id_type>& ingoing_vertices,
                                                        const std::vector<vertex_id_type>& outgoing_vertices,
                                                        const marking_t& marking) {
  tokens_t consumable_tokens_in_marking{};
  consumable_tokens_in_marking.reserve(ingoing_vertices.size());  // reserve upper bound
  std::ranges::for_each(ingoing_vertices,
                        [vertex_id, &marking, &consumable_tokens_in_marking](const vertex_id_type ingoing_vertex_id) {
                          const token_t ingoing_token{ingoing_vertex_id, vertex_id};
                          if (marking.contains(ingoing_token)) {
                            consumable_tokens_in_marking.push_back(ingoing_token);
                          }
                        });

  tokens_t producible_tokens_in_marking{};
  producible_tokens_in_marking.reserve(outgoing_vertices.size());
  std::ranges::transform(outgoing_vertices, std::back_inserter(producible_tokens_in_marking),
                         [&vertex_id](const vertex_id_type outgoing_id) {
                           return token_t{vertex_id, outgoing_id};
                         });

  transitions_t transitions{};
  transitions.reserve(consumable_tokens_in_marking.size() * producible_tokens_in_marking.size());

  // Create the transitions for each combination of ingoing token in the marking and each outgoing token
  for (const auto& consumed : consumable_tokens_in_marking) {
    for (const auto& produced : producible_tokens_in_marking) {
      transitions.emplace_back(transition{vertex_id, {consumed}, {produced}});
    }
  }

  return transitions;
}

}  // namespace

marking_t get_initial_marking(const bpmn_graph& model) {
  const auto outgoing_vertices{model.outgoing_vertices().at(model.single_start_vertex())};

  // Assume a start vertex can only have one outgoing edge
  debug_assert(outgoing_vertices.size() == 1);

  // Only the edge between start and its outgoing vertex has a token
  return marking_t{{model.single_start_vertex(), outgoing_vertices[0]}};
}

transitions_t get_enabled_vertex_transitions(const bpmn_graph& model, const marking_t& marking,
                                             vertex_id_type vertex_id) {
  if (!is_enabled(model, marking, vertex_id)) {
    return {};
  }

  const auto vertex_type_value{model.get_vertex(vertex_id).get_vertex_type()};

  const auto& ingoing_vertices{model.ingoing_vertices().at(vertex_id)};
  const auto& outgoing_vertices{model.outgoing_vertices().at(vertex_id)};

  // This is a vector since a single vertex can have multiple enabled transitions (exclusive gateway)
  return std::visit(
      ctl::overloaded{[&](const parallel& /*p*/) {
                        // If the gateway is enabled, it has one transition in which it consumes all of
                        // the ingoing tokens and produces tokens on all of the outgoing edges.
                        return transitions_t{
                            {compute_single_transition(vertex_id, ingoing_vertices, outgoing_vertices)}};
                      },
                      [&](const exclusive_choice& /*e*/) {
                        // An exclusive gateway has a transition for each combination of ingoing token in
                        // the marking and each outgoing edge
                        return compute_product_transitions(vertex_id, ingoing_vertices, outgoing_vertices, marking);
                      },
                      [&](const task& /*t*/) {
                        // This is designed to work with tasks that have multiple ingoing and multiple
                        // outgoing edges. Such a task has a transition for each combination of ingoing
                        // token in the marking and each outgoing edge
                        return compute_product_transitions(vertex_id, ingoing_vertices, outgoing_vertices, marking);
                      },
                      [&](const start& /*s*/) {
                        transitions_t transitions{};
                        transitions.reserve(outgoing_vertices.size());
                        std::ranges::transform(outgoing_vertices, std::back_inserter(transitions),
                                               [vertex_id](const vertex_id_type outgoing_vertex) {
                                                 return transition{vertex_id, {}, {{vertex_id, outgoing_vertex}}};
                                               });
                        return transitions;
                      },
                      [](const vertex_type& /*other*/) {
                        // All other nodes such as end do not have any transitions defined
                        return transitions_t{};
                      }},
      vertex_type_value);
}

bool is_enabled(const bpmn_graph& model, const marking_t& marking, const vertex_id_type vertex_id) {
  const auto type{model.get_vertex(vertex_id).get_vertex_type()};
  const auto ingoing_vertices{model.ingoing_vertices().at(vertex_id)};

  return std::visit(ctl::overloaded{[&](const parallel& /*p*/) {
                                      // A parallel gateway is enabled if all of its input edges are in the marking
                                      return std::ranges::all_of(ingoing_vertices, [&](const auto ingoing_vertex) {
                                        const token_t consumed_token{ingoing_vertex, vertex_id};
                                        return marking.contains(consumed_token);
                                      });
                                    },
                                    [&](const start& /*s*/) {
                                      // This only works for single object
                                      return marking.empty();
                                    },
                                    [&](const vertex_type& /*other*/) {
                                      // All other node types are enabled if at least one of its ingoing edges is marked
                                      return std::ranges::any_of(ingoing_vertices, [&](const auto ingoing_vertex) {
                                        const token_t consumed_token{ingoing_vertex, vertex_id};
                                        return marking.contains(consumed_token);
                                      });
                                    }},
                    type);
}

transitions_t get_enabled_transitions(const bpmn_graph& model, const marking_t& marking) {
  std::unordered_set<vertex_id_type> processed_vertices{};
  transitions_t result_vector{};

  // We only need to need to check if the end vertices of all edges in the marking are enabled, as other vertices in
  // the model are not enabled by definition of marking_t.
  for (const auto& token : marking) {
    // There can be multiple tokens in the marking that have the same target_id but we only need to check each once.
    if (processed_vertices.contains(token.get_target_id())) {
      continue;
    }
    processed_vertices.insert(token.get_target_id());

    // This is a vector since a single vertex can have multiple enabled transitions (exclusive gateway)
    auto enabled_transitions{get_enabled_vertex_transitions(model, marking, token.get_target_id())};
    result_vector.reserve(result_vector.size() + enabled_transitions.size());
    // Append the enabled transitions of this node to the result vector
    std::move(enabled_transitions.begin(), enabled_transitions.end(), std::back_inserter(result_vector));
    enabled_transitions.clear();  // Just to make it obvious this vector can not be used anymore
  }

  return result_vector;
}

// Marking is copied, so we can modify this to be the new marking
marking_t fire(marking_t marking, const transition& transition_to_fire) {
  // Remove all the consumed tokens
  for (const auto& consumed : transition_to_fire.consumed()) {
    const auto it{marking.find(consumed)};
    if (it == marking.end()) {
      throw common::cpm_exception{"Cannot fire transition, token [{}->{}] not found in marking.",
                                  consumed.get_source_id(), consumed.get_target_id()};
    }
    // We use the iterator for removal instead of the token value due to 'marking' being a multiset and we only want to
    // remove this single specific token (not all in case there are multiple).
    marking.erase(it);
  }

  // Add all the produced tokens
  marking.insert(std::begin(transition_to_fire.produced()), std::end(transition_to_fire.produced()));

  return marking;
}

marking_t fire(marking_t marking, const transitions_t& transitions_to_fire) {
  for (const auto& transition : transitions_to_fire) {
    marking = fire(marking, transition);
  }
  return marking;
}

marking_t fire_inverse(marking_t marking, const transition& transition_to_fire) {
  const auto swapped_transition{transition::swap_transition_direction(transition_to_fire)};
  return fire(std::move(marking), swapped_transition);
}

bool end_marking_reached(const bpmn_graph& model, const marking_t& marking) {
  return vertex_reached(model, marking, model.single_end_vertex());
}

bool vertex_reached(const bpmn_graph& model, const marking_t& marking, const vertex_id_type target) {
  // If any of the ingoing edges of the target vertex is in the marking, the vertex has been reached
  const auto& predecessors{model.ingoing_vertices().at(target)};
  return std::ranges::any_of(predecessors, [&](const vertex_id_type vertex) {
    return marking.contains(token_t{vertex, target});
  });
}

bool vertex_reached_inverse(const bpmn_graph& model, const marking_t& marking, const vertex_id_type target) {
  // If any of the ingoing edges of the target vertex is in the marking, the vertex has been reached
  const auto& successors{model.outgoing_vertices().at(target)};
  return std::ranges::any_of(successors, [&](const vertex_id_type vertex) {
    return marking.contains(token_t{target, vertex});
  });
}

std::optional<vertex_id_type> get_vertex_id_for_task(const bpmn_graph& model, const row_id activity_id) {
  if (!model.activity_id_to_vertex_id().contains(ctl::cast<activity_id_t>(activity_id))) {
    return std::nullopt;
  }
  return model.activity_id_to_vertex_id().at(ctl::cast<activity_id_t>(activity_id));
}

bool has_enabled_gateway_transitions(const bpmn_graph& model, const transitions_map_t& transitions_per_marking) {
  return std::ranges::any_of(transitions_per_marking, [&](const auto& it) {
    const transitions_t& transitions{it.second};
    return std::ranges::any_of(transitions, [&](const transition& transition) {
      const auto type{model.get_vertex(transition.vertex_id()).get_vertex_type()};
      return cpml::model::bpmn::is_gateway(type);
    });
  });
}

}  // namespace celonis::accelerator::operators::process::bpmn
