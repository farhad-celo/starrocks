#pragma once

#include <optional>
#include <string>

#include "modules/operators/process/alignment/input_output_mapper.h"
#include "modules/operators/process/petri_net/petri_net.h"

namespace celonis::accelerator {

class PetriNetDescription;

}  // namespace celonis::accelerator

namespace celonis::accelerator::operators::process::alignment::petri_net {

/// Returns string with pairs of (label, frequency) if any label is assigned to more than one transition
std::optional<std::string> get_duplicated_activities_string(const PetriNetDescription& petri_net_description);

/** Checks that petri_net_description corresponds to a valid Petri net:
 * 1- That all referenced nodes are defined
 * 2- That graph is bipartite
 * 3- At most one label assigned for each transition
 */
// TODO (goulart.e) add this check to the constructor of petri_net_representation
//  Then, check_is_workflow_net can be a method that operates on petri_net_representation
//  See github.com/celonis/cpm-query-engine/pull/1837#discussion_r776796296
void check_is_valid_petri_net(const PetriNetDescription& petri_net_description, const std::string& operator_name);

/**
 * Checks that petri_net_description corresponds to a workflow new,
 *  i.e. the net has one initial and one final marking, both with a single place marked with a single token
 *
 * Assumes that petri_net_description is a valid petri net, i.e. check_is_valid_petri_net was called before
 */
void check_is_workflow_net(const PetriNetDescription& petri_net_description, const std::string& operator_name);

/**
 * Extracts petri_net_representation from RLAlignOperatorNode.
 *  If labels strings in RLAlignOperatorNode can be found on the activity table,
 *      then use it's dictionary ID
 *  Else, assign a new ID which is not present in the dictionary
 *  It also separates between place-transition and transition-place arcs
 *
 *  The method assumes that every transition is mapped to one (and only one) label,
 *      therefore the method check_is_valid_petri_net must be called before
 */
petri_net_representation get_pn_repr_from_operator_input(const PetriNetDescription& petri_net_description,
                                                         string_to_int_mapper& str_mapper);

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
