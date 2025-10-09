#pragma once

#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_group.h"
#include "modules/operators/process/align_model/align_model_types.h"
#include "modules/operators/process/align_model/deviation_category.h"
#include "modules/operators/process/align_model/shared_types.h"
#include "modules/operators/process/bpmn/bpmn_from_proto.h"

namespace celonis::accelerator::operators::process::align_model::v1 {

/**
 * @brief Maps variants back to original traces and creates the table group both with internal joins between and
 * external joins to the activity column.
 *
 * Tables with columns:
 * Alignment: labels and move types - the label for a row is either the name of the activity or the bpmn vertex id of a
 * gateway, along with its type.
 *
 * Association: Has no added columns but has a non-zero number of rows - the individual edges are represented by the
 * join vector between the association and the alignment table and the association and edge class table.
 *
 * Edge Class: Each row in this table represents a separate edge component.
 *
 * @param alignments per variant alignment as produced by @see align_model
 * @param replay_results per variant replay results including relative timestamps as produced by @see
 * replay_aligned_variant
 * @param mapping petri net id to bpmn vertex id
 * @param bpmn_to_string bpmn vertex id to string e.g. start vertex with id 0 ([0 START]) --> "0 START"
 * @param variants the variant trace cache
 * @param activity_column The activity column of the original eventlog
 * @param case_id_column case_id column of the original eventlog
 * @param activity_to_case_join activity table to case table join - since the variants are at the case table level
 * @param model_cache_key bpmn model cache key
 * @param scope
 * @param context
 * @return table_group_stub The table group containing the alignment table, the association table and the edge class
 * table
 */
memory::table_group_t create_tables(const alignments_t& alignments, const replay_results_t& replay_results,
                                    const deviation_categories_for_cases_view_t deviation_categories,
                                    const bpmn::bpmn_to_string_t& bpmn_to_string, const variants& variants,
                                    const memory::column_t& activity_column, const memory::column_t& case_id_column,
                                    const memory::join_projection_vector_t& activity_to_case_join,
                                    const common::execution_context& context, size_t grain_size);

}  // namespace celonis::accelerator::operators::process::align_model::v1
