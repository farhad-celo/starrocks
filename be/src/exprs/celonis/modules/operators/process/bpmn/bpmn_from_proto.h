#pragma once

#include <string>
#include <unordered_map>

#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph.h>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/column_fwd.h"

namespace celonis::accelerator {
class BpmnModelDescription;
}

namespace celonis::accelerator::operators::process::bpmn {

/**
 * Creates a bpmn_graph from a protobuf message
 * @return The BPMN model.
 */
[[nodiscard]] cpml::model::bpmn_graph convert_from_proto(const BpmnModelDescription& bpmn_proto,
                                                         const memory::column_t& activity_column,
                                                         common::execution_context& operator_context);

using bpmn_to_string_t = std::unordered_map<cpml::model::bpmn::vertex_id_type, std::string>;
/**
 * Creates a bpmn_graph from the protobuf message, and creates strings for all vertices.
 *
 * In particular, note that the bpmn_graph only stores row ids into the activity column for its tasks.
 * Names of tasks that are not in the activity column can thus not be retrieved from the bpmn_graph alone.
 * On top of these task names, this also generates names for gateways that are consistent with the BPMN input.
 *
 * @return The BPMN model and a map from vertex ids to string identifiers
 */
std::pair<cpml::model::bpmn_graph, bpmn_to_string_t> convert_from_proto_and_create_string_map(
    const BpmnModelDescription& bpmn_proto, const memory::column_t& activity_column,
    common::execution_context& operator_context);

}  // namespace celonis::accelerator::operators::process::bpmn