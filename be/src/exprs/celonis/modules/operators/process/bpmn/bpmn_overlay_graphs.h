#pragma once

#include <vector>

#include "modules/operators/process/bpmn/bpmn_graph_fwd.h"
#include "modules/operators/process/bpmn/bpmn_graph_with_block_structure.h"
#include "modules/operators/process/bpmn/edge.h"

namespace celonis::accelerator::operators::process::bpmn {

/**
 * Overlays the rhs BPMN graph with the lhs BPMN graph. The lhs BPMN graph is used as a base and the rhs BPMN graph is
 * overlaid. All edges added from the rhs BPMN graph to the lhs BPMN graph get the rhs_object_id assigned. The overlay
 * happens on task level if a task of lhs BPMN graph is equivalent with a task in the rhs BPMN graph. Equivalency is
 * defined on an activity id level (not to be confused with event id, which is on instance level).
 *
 * @param lhs BPMN graph used as base.
 * @param rhs BPMN graph to be overlaid on top of lhs.
 * @param rhs_object_id All edges which are overlaid from rhs BPMN graph to lhs BPMN graph get the object id assigned.
 * @return Overlaid BPMN graph.
 */
[[nodiscard]] bpmn_graph overlay(const bpmn_graph& lhs, const bpmn_graph& rhs, object_id rhs_object_id = 0);
/** Same as above but for graphs with block structure */
[[nodiscard]] bpmn_graph_with_block_structure overlay(const std::vector<bpmn_graph_with_block_structure>& graphs,
                                                      object_id initial_object_id = {});

}  // namespace celonis::accelerator::operators::process::bpmn
