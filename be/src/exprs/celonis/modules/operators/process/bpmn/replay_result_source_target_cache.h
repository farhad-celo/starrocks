#pragma once

#include <string>
#include <utility>

#include <boost/container_hash/hash.hpp>

#include "legacy_embedded_ctl/cache_fwd.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/operators/process/bpmn/bpmn_graph_fwd.h"
#include "modules/operators/process/bpmn/replay_result_source_target_fwd.h"

namespace celonis::accelerator::operators::process::bpmn {

/**
 * If no BPMN replay result exists in the cache, performs the replay of the BPMN
 * model to compute node ids and event row indexes for each source/target node of a passed edge in the BPMN model.
 * Additionally, it creates the join vector from the result table to the case table.
 */
[[nodiscard]] replay_result_source_target_t create_replay_result(
    const bpmn_graph& model, const memory::column_t& input_column, const memory::column_t& activity_column,
    const memory::column_t& case_id_column, const memory::join_projection_vector_t& activity_case_join_index,
    common::execution_context& context);

using replay_result_source_target_cache_t =
    legacy_embedded_ctl::cache<std::pair<std::string, std::string>, replay_result_source_target_t,
               boost::hash<std::pair<std::string, std::string>>>;

}  // namespace celonis::accelerator::operators::process::bpmn