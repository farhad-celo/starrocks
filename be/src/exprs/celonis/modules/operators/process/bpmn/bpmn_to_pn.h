#pragma once

#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/bpmn/bpmn_graph_fwd.h"

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

alignment::petri_net::petri_net_representation get_pn(const bpmn_graph& bpmn_model);
}  // namespace celonis::accelerator::operators::process::bpmn