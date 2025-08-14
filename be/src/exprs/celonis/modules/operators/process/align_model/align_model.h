#pragma once

#include <string>
#include <utility>

#include <cpml/conformance/alignment_types.h>
#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph_fwd.h>

#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/operators/process/align_model/align_model_statistics.h"
#include "modules/operators/process/align_model/align_model_types.h"
#include "modules/operators/process/align_model/shared_types.h"

namespace celonis::accelerator::operators::process::align_model {

/**
 * @brief Aligns the variants in the variants log with the petri net model
 *
 * @param variants The variants to align
 * @param petri_net A Petri net
 * @param mapper The mapper from Petri net labels to label ids. Needs to be initialized with the activity column dict.
 * @param config Configuration parameters of the align model algorithm
 * @param context The execution context we're running in
 * @return alignments_t The alignments of all variants, and a binary relation of parallel activities
 *
 * Note: We compute the behavioral profile for the relaxation-labeling, and extract the parallel relation from there.
 */
std::pair<alignments_t, cpml::conformance::behavioral_relations> align_model(
    const memory::cache::variant_trace_cache_t& variants, const cpml::model::bpmn_graph& bpmn_model,
    const align_model_config& config, align_model_statistics& stats, const std::string& activity_table_name,
    const common::execution_context& context);
/**
 * @brief Replays the aligned variants on the model creating their partial execution orders.
 * Also creates joins for all synchronous and model moves to the alignments
 *
 * @param petri_net A safe Petri net
 * @param alignment An alignment table as produced by @see align_model
 * @param config Configuration parameters of the align model algorithm
 * @return replay_results_t The partial order executions of all variants as well as groupers and edge types
 */
replay_results_t replay_aligned_variants(const cpml::model::bpmn_graph& bpmn_graph, alignments_view_t alignments,
                                         const cpml::conformance::behavioral_relations& parallel_vertices,
                                         const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process::align_model
