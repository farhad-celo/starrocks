#pragma once

#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_group.h"
#include "modules/operators/process/align_model/align_model_types.h"
#include "modules/operators/process/align_model/deviation_category.h"
#include "modules/operators/process/align_model/shared_types.h"
#include "modules/operators/process/align_model/v2/create_alignment_output_projection.h"
#include "modules/operators/process/bpmn/bpmn_from_proto.h"

namespace celonis::accelerator::operators::process::align_model::v2 {

/**
 * @brief Maps variants back to original traces and creates the table group both with internal joins between and
 * external joins to the activity column. Produces 1 Alignment table and 7 pairs of Association and Edge Class tables
 * for a total of 15 tables in the table group.
 *
 * Tables with columns:
 * Alignment Table:
 * - Vertex Label Column: The label is either the name of the activity or one of BPMN_START, BPMN_END, BPMN_PARALLEL or
 * BPMN_EXCLUSIVE_CHOICE denoting their respective gateways.
 * - Move Type column:  The move type, one of @see alignment_move_type
 * - Model Vertex ID:  If the type is GATEWAY, SYNC OR MODEL the vertex ID of the BPMN vertex, if LOG or UNMAPPED null
 *
 * For each @see edge_type a pair of association and edge class table
 * Association Table:
 * - Edge class column: An int column with edge class IDs, there are duplicate edge class IDs in blocks (i.e. the column
 * is partitioned by them)
 *
 * Edge Class Table:
 * - Edge class ID column: Contains each edge class ID column also found in the association table. This column is the
 * PK, i.e. unique and non-null, it is joined to the association table on this column.
 * - Edge Class Type column: The type of this specific edge class, one of @see edge_type
 *
 * Each Association and Edge Class pair is joined internally like
 * Association <-n---1-> Edge Class
 *
 * Furthermore each association is joined to the alignment:
 * Alignment <-1---n-> Association Type
 *
 * And the Activity table joins to the alignment
 * Activity <-1---n-> Alignment
 *
 *
 * @param alignments per variant alignment as produced by @see align_model
 * @param replay_results per variant replay results including relative timestamps as produced by @see
 * replay_aligned_variant
 * @param bpmn_to_string BPMN vertex ID to string e.g. start vertex with id 0 ([0 START]) --> "0 START"
 * @param activity_column The activity column of the original eventlog
 * @param case_id_column case_id column of the original eventlog
 * @param activity_to_case_join activity table to case table join - since the variants are at the case table level
 * @return memory::table_group_t The table group containing the alignment table, the association tables and the edge
 * class tables
 */
template <compute_incomplete_category COMPUTE_V2>
memory::table_group_t create_tables(const alignments_t& alignments, const replay_results_t& replay_results,
                                    const deviation_categories_for_cases_t& deviation_categories,
                                    const bpmn::bpmn_to_string_t& bpmn_to_string, const variants& variants,
                                    const memory::column_t& activity_column, const memory::column_t& case_id_column,
                                    const memory::join_projection_vector_t& activity_to_case_join,
                                    const common::execution_context& context, size_t grain_size,
                                    const create_alignment_output_projection& output_projection);

}  // namespace celonis::accelerator::operators::process::align_model::v2
