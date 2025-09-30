#pragma once

#include <optional>
#include <vector>

#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph_fwd.h>

#include "modules/operators/process/bpmn/replay_types.h"

namespace celonis::accelerator::operators::process::bpmn {

/*
 * Returns a set of initial markings. Each initial marking has one of the outgoing edges of a start node present.
 */
[[nodiscard]] marking_t get_initial_marking(const cpml::model::bpmn_graph& model);

/*
 * Returns all enabled transitions of a given vertex_id for a specific BPMN model and marking. If the vertex is not
 * enabled, an empty vector is returned.
 */
[[nodiscard]] transitions_t get_enabled_vertex_transitions(const cpml::model::bpmn_graph& model,
                                                           const marking_t& marking,
                                                           cpml::model::bpmn::vertex_id_type vertex_id);

/*
 * Checks if a given vertex_id in the BPMN model is enabled based on the tokens that are present in the marking.
 */
[[nodiscard]] bool is_enabled(const cpml::model::bpmn_graph& model, const marking_t& marking,
                              cpml::model::bpmn::vertex_id_type vertex_id);

/*
 * For a given BPMN model and marking, returns all the transitions that can fire.
 */
[[nodiscard]] transitions_t get_enabled_transitions(const cpml::model::bpmn_graph& model, const marking_t& marking);

/*
 * Fires a transition on a marking by removing all consumed edges and adding all produced edges.
 */
[[nodiscard]] marking_t fire(marking_t marking, const transition& transition_to_fire);

/*
 * Fires multiple transition on a marking by calling fire for each transition.
 */
[[nodiscard]] marking_t fire(marking_t marking, const transitions_t& transitions_to_fire);

/*
 * The inverse operation of firing a marking. Removes all produced edges from the marking and adds all consumed edges.
 */
[[nodiscard]] marking_t fire_inverse(marking_t marking, const transition& transition_to_fire);

/**
 * Checks if any of the candidate markings enables the end node.
 * @return If found, returns the corresponding marking.
 */
[[nodiscard]] bool end_marking_reached(const cpml::model::bpmn_graph& model, const marking_t& marking);

/*
 * Checks if the given target vertex is reached by a token on any of the ingoing edges.
 */
[[nodiscard]] bool vertex_reached(const cpml::model::bpmn_graph& model, const marking_t& marking,
                                  cpml::model::bpmn::vertex_id_type target);

/*
 * Checks if the given target vertex has a token on any of its ingoing edges.
 */
[[nodiscard]] bool vertex_reached_inverse(const cpml::model::bpmn_graph& model, const marking_t& marking,
                                          cpml::model::bpmn::vertex_id_type target);

/**
 * Returns empty optional if the activity id is not present in the bpmn model
 */
[[nodiscard]] std::optional<cpml::model::bpmn::vertex_id_type> get_vertex_id_for_task(
    const cpml::model::bpmn_graph& model, row_id activity_id);

// TODO(n.weber): Tests in case still needed in the future after rewrite (CPL-7268)
[[nodiscard]] bool has_enabled_gateway_transitions(const cpml::model::bpmn_graph& model,
                                                   const transitions_map_t& transitions_per_marking);

}  // namespace celonis::accelerator::operators::process::bpmn