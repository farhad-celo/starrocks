#pragma once

#include "modules/common/int_types.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

/**
 * "A < B" meaning "B can be reached from A". The behavioral_relations are:
 * A PRECEDES B: If A < B and B !< A
 * A FOLLOWS B: If B PRECEDES A
 * A EXCLUSIVE B: If A !< B and B !< A
 * A INTERLEAVED B: If A < B and B < A
 */
enum class behavioral_relation : int8_t { PRECEDES, FOLLOWS, EXCLUSIVE, INTERLEAVED };

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
