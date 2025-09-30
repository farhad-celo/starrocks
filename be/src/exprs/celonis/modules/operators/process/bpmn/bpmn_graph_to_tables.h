#pragma once

#include <variant>
#include <vector>

#include <cpml/model/bpmn_graph_fwd.h>
#include <cpml/model/bpmn_graph_with_block_structure.h>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/dictionary_fwd.h"
#include "modules/memory/row_id.h"
#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::operators::process::bpmn {

struct bpmn_tables {
  memory::table_t bpmn_edges;
  memory::table_t bpmn_nodes;
  memory::table_t bpmn_activities;
  memory::table_t bpmn_model_descriptions;
  memory::table_t bpmn_blocks;
  memory::table_t bpmn_nodes_to_blocks;
};

/**
 * Creates a table representation for a given (multi-object) BPMN graph.
 *
 * The result consists of six tables:
 * - bpmn_edges: Contains for each edge the source id, target it, object id and the amount of object entities flowing
 * through the edge
 * - bpmn_nodes: Contains for each node of the graph its id and node type
 * - bpmn_activities: Contains for each node of the graph that is an activity its node id, the activity name, whether
 * the activity is visible in the reduced version of the graph and the number of occurences for the activity
 * - bpmn_model_descriptions: the string representation of the bpmn model (e.g., used in other BPMN related operators)
 * - bpmn_nodes_to_blocks: A mapping from node IDs to their corresponding related block (ID)
 * - bpmn_blocks: All blocks of the BPMN model
 *
 * @param graph The BPMN graph to be converted to table format.
 * @param activity_dict A global dictionary of all the activities used in the graph, mapping their IDs to activity
 * names.
 * @return Three tables describing the given graph.
 */
bpmn_tables create_bpmn_tables_from_bpmn_graph(const cpml::model::bpmn_graph_with_block_structure& graph,
                                               const memory::dictionary_t& activity_dict,
                                               memory::table_row_limit_t table_row_limit,
                                               const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::bpmn