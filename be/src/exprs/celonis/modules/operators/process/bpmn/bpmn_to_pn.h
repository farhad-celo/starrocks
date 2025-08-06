#pragma once

#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph_fwd.h>

#include "modules/operators/process/petri_net/petri_net.h"

/** !!! This code was migrated to the CPML but for legacy reasons kept here as well because other code in Saola relied
 * on it. If you change anything here, make sure to also make this corresponding change in the CPML!!!
 * TODO(n.weber): With PMT-1819 we can expose this logic in the CPML directly and remove this file
 */
namespace celonis::accelerator::operators::process::bpmn {
/*
 * Based Definition 11 (Petrify) from van der Aalst, Wil MP, Alexander Hirnschall, and H. M. W. Verbeek.
 * "An alternative way to analyze workflow graphs."
 * Parallels are handles similar to tasks and included in T
 *
 * One important difference between the approach described in the paper and the one implemented here is that we create
 * transitions for exclusive, start ,and end nodes. Each transition with a non tau label can be mapped to the
 * corresponding bpmn node via the label=vertex_id
 *
 * Some additional information on the naming scheme:
 * - tasks have a t prefix, places a p prefix
 * - some task and places follow a scheme <source_id>_<target_id>
 *
 */

alignment::petri_net::petri_net_representation get_pn(const cpml::model::bpmn_graph& bpmn_model);

// Mapping from transition_ids in petri net representation to corresponding bpmn vertices
//  All transitions related to vertices should be here and no transitions related to edges should be here
using petri_net_str_id_to_bpmn_mapping = std::unordered_map<std::string, cpml::model::bpmn::vertex_id_type>;
using petri_net_id_to_bpmn_mapping = std::unordered_map<alignment::petri_net::petri_net_transition_id,
                                                        cpml::model::bpmn::vertex_id_type, alignment::petri_net::hash_transition>;
// We need this because we want log moves to point to tasks in the BPMN graph
//  this only contains labels which are part of the log
using label_to_bpmn_mapping =
    std::unordered_map<alignment::petri_net::petri_net_representation::label_type, cpml::model::bpmn::vertex_id_type>;

struct bpmn_to_petri_net_result_t {
  alignment::petri_net::petri_net_representation petri_net;
  petri_net_str_id_to_bpmn_mapping pn_str_id_to_bpmn;
  label_to_bpmn_mapping log_label_to_bpmn;
};

/** Flag to configure the BPMN -> PN conversion such that (often unwanted) BPMN edge transitions are removed */
enum class filter_out_bpmn_edge_transitions : bool { YES = true, NO = false };

/**
 * @brief Converts a BPMN graph into a semantically equivalent Petri net.
 *
 * @tparam DO_FILTER_OUT_BPMN_EDGE_TRANSITIONS When this is set to 'YES', BPMN edge transitions are removed from the PN
 * @param graph The BPMN graph
 * @return bpmn_to_petri_net_result_t A pair consisting of the petri net and the mapping from its transitions back to
 * the BPMN model
 */
template <filter_out_bpmn_edge_transitions DO_FILTER_OUT_BPMN_EDGE_TRANSITIONS = filter_out_bpmn_edge_transitions::NO>
[[nodiscard]] bpmn_to_petri_net_result_t bpmn_to_petri_net_with_mapping(const cpml::model::bpmn_graph& graph);

/** Same as above but only returns the produced petri net without any mappings to the bpmn graph */
template <filter_out_bpmn_edge_transitions DO_FILTER_OUT_BPMN_EDGE_TRANSITIONS = filter_out_bpmn_edge_transitions::NO>
[[nodiscard]] alignment::petri_net::petri_net_representation bpmn_to_petri_net(const cpml::model::bpmn_graph& graph);

}  // namespace celonis::accelerator::operators::process::bpmn