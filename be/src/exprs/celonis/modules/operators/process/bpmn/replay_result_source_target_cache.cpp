#include "replay_result_source_target_cache.h"

#include "modules/operators/process/bpmn/replay.h"
#include "modules/operators/process/bpmn/replay_result_source_target.h"

namespace celonis::accelerator::operators::process::bpmn {

replay_result_source_target_t create_replay_result(const cpml::model::bpmn_graph& model,
                                                   const memory::column_t& input_column,
                                                   const memory::column_t& activity_column,
                                                   const memory::column_t& case_id_column,
                                                   const memory::join_projection_vector_t& activity_case_join_index,
                                                   common::execution_context& context) {
  return std::make_shared<replay_result_source_target>(replay_eventlog_for_source_target(
      model, input_column, activity_column, case_id_column, activity_case_join_index, context));
}

}  // namespace celonis::accelerator::operators::process::bpmn