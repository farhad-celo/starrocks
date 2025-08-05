#pragma once

#include "legacy_embedded_ctl/static_array.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/bpmn/bpmn_graph_fwd.h"
#include "modules/operators/process/bpmn/bpmn_graph_with_block_structure.h"

namespace celonis::accelerator::operators::process::bpmn {

[[nodiscard]] bpmn_graph remap_task_ids(const bpmn_graph& graph, const legacy_embedded_ctl::static_array<row_id>& mapping_vector);
[[nodiscard]] bpmn_graph_with_block_structure remap_task_ids(const bpmn_graph_with_block_structure& graph,
                                                             const legacy_embedded_ctl::static_array<row_id>& mapping_vector);

}  // namespace celonis::accelerator::operators::process::bpmn
