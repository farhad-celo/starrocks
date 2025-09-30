#pragma once

#include <cpml/model/bpmn_graph_fwd.h>
#include <ctl/bitset_fwd.h>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/operators/process/bpmn/replay_result_source_target.h"

namespace celonis::accelerator::operators::process::bpmn {

/**
 * Replays the eventlog on a bpmn model and generates a result which can be consumed by the MO_BPMN_SOURCE /
 * MO_BPMN_TARGET operators.
 */
[[nodiscard]] replay_result_source_target replay_eventlog_for_source_target(
    const cpml::model::bpmn_graph& model, const memory::column_t& input_column, const memory::column_t& activity_column,
    const memory::column_t& case_id_column, const memory::join_projection_vector_t& activity_case_join_index,
    common::execution_context& parent_context);

/**
 * Replays the eventlog on a bpmn model and generates true/false diagnostics per case
 */
[[nodiscard]] ctl::dynamic_bitset_t replay_eventlog_for_conformance(const cpml::model::bpmn_graph& model,
                                                                    const memory::column_t& activity_column,
                                                                    const memory::column_t& case_id_column,
                                                                    common::execution_context& parent_context);

}  // namespace celonis::accelerator::operators::process::bpmn