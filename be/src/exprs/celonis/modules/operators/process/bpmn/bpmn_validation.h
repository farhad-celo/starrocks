#pragma once

#include "modules/operators/process/bpmn/bpmn_graph_fwd.h"

namespace celonis::accelerator::operators::process::bpmn {

/**
 * @brief Validates the given BPMN model for consistency of the internal data structures. This is checked for *every*
 * `bpmn_graph` we construct. Perform only simple & cheap validations here to ensure that we will encounter no fatal
 * errors.
 * @param bpmn_model the BPMN model to validate
 * @throws common::cpm_exception if inconsistencies in the model are detected
 */
void validate_bpmn_model_consistency(const bpmn_graph& bpmn_model);

/**
 * @brief Validates that the given BPMN model conforms to our constraints. More expensive checks that do not need to be
 * performed for every graph are supposed to be performed here. Usage heavily encouraged for testing code to validate
 * that the models we produce fit our assumptions and in non-performance-critical sections, e.g. if we encounter an
 * exception in code working with `bpmn_graph` this method could provide additional diagnostics in case there are
 * undesired structures in the graph.
 * @param bpmn_model the BPMN model to validate
 * @throws common::cpm_exception if inconsistencies in the model are detected
 */
void validate_bpmn_model_constraints(const bpmn_graph& bpmn_model);

}  // namespace celonis::accelerator::operators::process::bpmn
