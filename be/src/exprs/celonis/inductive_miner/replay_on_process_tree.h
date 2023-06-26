#pragma once

#include "modules/cube/filter_bitset.h"
#include "modules/memory/column.h"
#include "modules/operators/process/inductive_miner/process_tree.h"

namespace celonis::accelerator::operators::process {

/**
 * Given a process tree and an event-log (a pair of matching activity and case columns), replay the traces on top of the
 * process tree
 *
 * Similar to the inductive miner, we accept an additional parameter for filtering the event-log.
 *
 * TODO(a.swoboda) We could also return the ids of traces that we could not replay
 *
 * @param tree The process tree to replay on. Can contain any (including inconsistent) object counts.
 * @param activity_column The activity column of the event-log.
 * @param case_column The case column of the event-log.
 * @param eventlog_selections A bitset to select entries in the event-log. We only use rows with a set bit.
 * @param context The execution context that we are running in.
 * @return A process tree with the exact same structure as the input tree, but with the counts from replaying.
 */
process_tree replay_on_process_tree(process_tree tree, const memory::column_t& activity_column,
                                    const memory::column_t& case_column,
                                    const cube::filter_bitset_t& eventlog_selections,
                                    const common::execution_context& context);

}  // namespace celonis::accelerator::operators::process
