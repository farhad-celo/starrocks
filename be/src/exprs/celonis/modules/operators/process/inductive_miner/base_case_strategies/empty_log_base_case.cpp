#include "../base_case_strategy.h"

namespace celonis::accelerator::operators::process {

// Only happens if the input event log is empty (for example, all cases are filtered out).
// Does not happen after another split
bool empty_log_base_case::is_applicable(const directly_follows_graph& dfg) { return boost::num_vertices(dfg) == 0; }

process_tree empty_log_base_case::apply() { return process_tree{process_tree::tau{0}}; }

}  // namespace celonis::accelerator::operators::process
