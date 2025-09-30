#pragma once

#include <cpml/model/bpmn_graph_fwd.h>

#include "modules/common/shared_types_fwd.h"

namespace celonis::accelerator::operators::process::bpmn {

/**
 * Calculates the density of a single object bpmn_graph
 *
 * @param bpmn_graph The BPMN model for which to calculate density.
 * @return The density of the model in [0;1].
 */
[[nodiscard]] cel_float_t calculate_density(const cpml::model::bpmn_graph& bpmn_graph);
}  // namespace celonis::accelerator::operators::process::bpmn