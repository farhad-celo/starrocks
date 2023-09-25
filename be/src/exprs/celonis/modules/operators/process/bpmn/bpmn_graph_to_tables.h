#pragma once

#include <variant>
#include <vector>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/dictionary_fwd.h"
#include "modules/memory/row_id.h"
#include "modules/memory/table_fwd.h"
#include "modules/operators/process/bpmn/bpmn_graph_fwd.h"

namespace celonis::accelerator::operators::process::bpmn {

struct bpmn_tables {
  memory::table_t bpmn_edges;
  memory::table_t bpmn_nodes;
  memory::table_t bpmn_activities;
  memory::table_t coverage;
  memory::table_t bpmn_model_descriptions;
};

/**
 * Creates a table representation for a given (multi-object) BPMN graph.
 *
 * The result consists of three tables:
 * - bpmn_edges: Contains for each edge the source id, target it, object id and the amount of object entities flowing
 * through the edge
 * - bpmn_nodes: Contains for each node of the graph its id and node type
 * - bpmn_activities: Contains for each node of the graph that is an activity its node id, the activity name, whether
 * the activity is visible in the reduced version of the graph and the number of occurences for the activity
 *
 * @param graph The BPMN graph to be converted to table format.
 * @param activity_dict A global dictionary of all the activities used in the graph, mapping their IDs to activity
 * names.
 * @return Three tables describing the given graph.
 */
bpmn_tables create_bpmn_tables_from_bpmn_graph(const bpmn_graph& graph, const memory::dictionary_t& activity_dict,
                                               const std::vector<row_id>& coverage,
                                               memory::table_row_limit_t table_row_limit,
                                               const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::bpmn