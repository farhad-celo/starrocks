#pragma once

#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/bpmn_graph_with_block_structure.h"
#include "modules/operators/process/bpmn/edge.h"
#include "modules/operators/process/inductive_miner/process_tree_fwd.h"

namespace celonis::accelerator::operators::process::bpmn {

/**
 * Converts a process tree into a BPMN graph by applying the transformations steps outlined in this publication,
 * http://bpmcenter.org/wp-content/uploads/reports/2015/BPM-15-01.pdf.
 *
 * @param process_tree The process tree to be converted.
 * @return Converted BPMN model consisting of start and end, tasks, exclusive choice and parallel gateways.
 */
[[nodiscard]] bpmn_graph convert_to_bpmn_graph(const process_tree& process_tree, object_id object = {});

/** Same as 'convert_to_bpmn_graph' but with adding block structure information */
[[nodiscard]] bpmn_graph_with_block_structure convert_to_bpmn_graph_with_block_structure(
    const process_tree& process_tree, object_id oid = {});

}  // namespace celonis::accelerator::operators::process::bpmn
