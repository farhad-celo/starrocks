#include "align_model_table_group_node.h"

#include <algorithm>
#include <memory>
#include <string>

#include <fmt/core.h>

#include "align_model.h"
#include "align_model_statistics.h"
#include "modules/common/execution_context.h"
#ifndef CELOSTAR
#include "modules/common/hash_cache_key.h"
#include "modules/common/shared_types.h"
#include "modules/cube/event_table_config.h"
#include "modules/cube/event_table_config_manager.h"
#include "modules/cube/execution/tracking/operator_tracker.h"
#include "modules/cube/query_scope.h"
#endif
#include "modules/memory/column.h"
#include "modules/memory/table.h"
#include "modules/memory/table_group.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"
#include "modules/operators/aggregation/string_aggregation.h"
#ifndef CELOSTAR
#include "modules/operators/framework/cached_operator_fwd.h"
#endif
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/query/operators.pb.h"
#include "replay_aligned_variant.h"

namespace celonis::accelerator::operators::process::align_model {

namespace {
constexpr size_t CREATE_TABLE_GRAIN_SIZE{100'000};
#ifndef CELOSTAR
void sanitize_activity_table(const memory::column_t& activity_column, const std::string_view operator_name,
                             const cube::query_scope& scope, const common::execution_context& context) {
  // ensure that the columns actually belongs to an activity table
  const auto column_is_from_activity_table{
      scope.get_event_table_config_manager().is_activity_table(activity_column->get_owner())};

  if (!column_is_from_activity_table) {
    throw common::cpm_exception{
        "{}: expected activity column to belong to an activity table but instead received {}. Process operators can "
        "only be used when a process is configured.",
        operator_name, activity_column->get_user_visible_name(context)};
  }
}

void sanitize_align_model_inputs(const memory::column_t& activity_column, const memory::column_t& case_id_column,
                                 const std::string_view operator_name, const common::execution_context& context) {
  // check errors before calling the method on the registry (that would cache the exception)
  if (activity_column->get_owner() != case_id_column->get_owner()) {
    throw common::cpm_exception{
        "{}: activity column and case column belong to different tables: {} and {} respectively.", operator_name,
        activity_column->get_user_visible_name(context), case_id_column->get_user_visible_name(context)};
  }

  // make sure that the activity column is a string column from the activity table
  if (activity_column->get_data_type() != data_type::cel_string) {
    throw common::cpm_exception{"{}: expected activity column to be of type STRING but it is of type {}.",
                                operator_name, convert_to_string(activity_column->get_data_type())};
  }

  if (case_id_column == nullptr) {
    throw common::cpm_exception{"{}: No case column defined for event table {}.", operator_name,
                                activity_column->get_user_visible_name(context)};
  }
}
#endif
}  // namespace

#ifndef CELOSTAR
align_model_table_group_node::align_model_table_group_node(cube::query_scope& scope,
                                                           const TableGroupNode_AlignModelTableGroupNode& node)
    : scope_{scope}, node_{node}, registry_{scope.get_align_model_registry()} {
  common::runtime_assert(node.has_activity_expression(),
                         "Message is missing an activity. An activity expression is required for {}",
                         get_user_visible_operator_name());
  common::runtime_assert(node.has_model(), "Message is missing a model. A model is required for {}",
                         get_user_visible_operator_name());
}

std::string align_model_table_group_node::make_table_group_cache_key(const memory::column_t& activity_column,
                                                                     const BpmnModelDescription& model,
                                                                     const common::execution_context& context) {
  legacy_embedded_debug_assert(!activity_column->get_user_visible_name(context).empty());
  return fmt::format("$${}-{}-{}$$", get_user_visible_operator_name(), activity_column->get_user_visible_name(context),
                     model.cache_key());
}

std::string align_model_table_group_node::make_pruned_variant_cache_key(const memory::column_t& activity_column,
                                                                        const BpmnModelDescription& model,
                                                                        const common::execution_context& context) {
  legacy_embedded_debug_assert(!activity_column->get_user_visible_name(context).empty());
  std::set<std::string> ordered_model_node_names{};
  for (const auto& node : model.nodes()) {
    if (node.node_type() == celonis::accelerator::BpmnModelDescription_BpmnNode_BpmnNodeType_TASK &&
        node.has_task_name()) {
      ordered_model_node_names.insert(node.task_name());
    }
  }
  return fmt::format("$${}-{}-{}$$", get_user_visible_operator_name(), activity_column->get_user_visible_name(context),
                     ordered_model_node_names);
}
#endif

#ifdef CELOSTAR
memory::table_group_t create_align_model_tables::operate(const common::execution_context& context) {
#else
memory::table_group_t create_align_model_tables::operator()([[maybe_unused]] cube::cube_data_model& data_model,
                                                            const memory::management::swap_info& sinfo,
                                                            const common::execution_context& context) {
#endif
  align_model_statistics stats{};
  auto align_model_op_context{context.create_sub_context("create_align_model_tables::operator()", {})};
#ifndef CELOSTAR
  auto cache_key{align_model_table_group_node::make_table_group_cache_key(activity_column_, model_description_,
                                                                          align_model_op_context)};
#endif

  const auto [bpmn_graph, bpmn_to_string]{
      bpmn::convert_from_proto_and_create_string_map(model_description_, activity_column_, align_model_op_context)};
  const auto model_with_mapping{bpmn_to_petri_net(bpmn_graph)};

#ifdef CELOSTAR
  const auto variants{aggregation::compute_variant_row_ids(
      {.table_one_side = case_table_.get(), .column_n_side = activity_column_, .projection = activity_to_case_join_},
      context)};
#else
  const auto* const activity_table{activity_column_->get_owner()};
  auto* const case_table{
      scope_.get_event_table_config_manager().get_event_table_config(activity_table, context)->case_table.get()};
  const memory::join_projection_vector_t activity_to_case_join{
      scope_.get_join_projection(activity_table, case_table, align_model_op_context)};
  const auto variants{aggregation::compute_variant_row_ids(
      {.table_one_side = case_table,
       .column_n_side = activity_column_,
       .projection = scope_.get_join_projection(activity_table, case_table, align_model_op_context)},
      scope_.get_variant_trace_cache_manager(), align_model_op_context)};

  const operator_input_columns_t input_columns{activity_column_, case_table->get_column_header(0)};
#endif

  // TODO(j.kruska): CPL 8890 Clean all these different cache keys up
#ifdef CELOSTAR
  auto config{align_model_config::make("CACHE_KEY_PRUNED_VARIANTS", variant_trace_cache_manager_, log_aligner_cfg_)};
#else
  const auto pruned_variant_cache_key{
      fmt::format("$${}$$PRUNED_VARIANTS$$", align_model_table_group_node::make_pruned_variant_cache_key(
                                                 activity_column_, model_description_, align_model_op_context))};
  auto* trace_cache_manager{std::addressof(scope_.get_variant_trace_cache_manager())};
  auto config{align_model_config::make(pruned_variant_cache_key, trace_cache_manager, log_aligner_cfg_)};
#endif

  // per variant alignments and replay results
  // while the sub-spans/datadog traces contain this timing information, tracing is not always enabled
  common::timer alignment_timer{};
  const auto [alignments, parallel_vertices]{align_model(
      variants, model_with_mapping, config, stats, activity_column_->get_owner()->get_name(), align_model_op_context)};
  alignment_timer.stop();
  stats.time_variant_alignment = alignment_timer.duration_us().count();

  common::timer replay_timer{};
  const auto replay_results{replay_aligned_variants(bpmn_graph, alignments, parallel_vertices, align_model_op_context)};
  replay_timer.stop();
  stats.time_variant_replay = replay_timer.duration_us().count();

#ifdef CELOSTAR
    return  create_tables(alignments, replay_results, bpmn_to_string, variants, activity_column_, case_column_,
                          activity_to_case_join_, align_model_op_context, CREATE_TABLE_GRAIN_SIZE);
#else
  cube::registration_options options{.sinfo = sinfo,
                                     .cache_key = cache_key,
                                     .user_visible_name = cache_key,
                                     .scope = scope_,
                                     .schema_info = data_model.get_schema_info()};

  common::timer inflation_timer{};
  auto tables{create_tables(alignments, replay_results, bpmn_to_string, variants, activity_column_, case_column_,
                            activity_to_case_join, options, align_model_op_context, CREATE_TABLE_GRAIN_SIZE,
                            data_model.get_input_dependencies(), input_columns)};
  inflation_timer.stop();

  stats.time_inflation = inflation_timer.duration_us().count();
  stats.log_to_operator_statistics(add_telemetry_counter_);

  for ([[maybe_unused]] const auto& [name, table] : tables->get_tables()) {
    data_model.get_tables().add_table(table);
  }
  return tables;
#endif
}

#ifndef CELOSTAR
memory::table_group_t align_model_table_group_node::do_execute(cube::execution::tracking::operator_tracker& tracker,
                                                               cube::operator_executor& executor,
                                                               common::execution_context& operator_context) const {
  // extract input data
  const auto activity_column{scope_.get_session_operator_node(node_.activity_expression().operator_ref_id())
                                 ->execute(tracker, executor, operator_context)};
  sanitize_activity_table(activity_column, get_user_visible_operator_name(), scope_, operator_context);
  const auto* event_log_config{
      scope_.get_event_table_config_manager().get_event_table_config(activity_column->get_owner(), operator_context)};
  memory::column_t case_column{node_.has_case_expression()
                                   ? scope_.get_session_operator_node(node_.case_expression().operator_ref_id())
                                         ->execute(tracker, executor, operator_context)
                                   : event_log_config->case_id_column};
  const auto& model{node_.model()};

  sanitize_align_model_inputs(activity_column, case_column, get_user_visible_operator_name(), operator_context);

  auto add_telemetry_counter{tracker.telemetry_counter_callback_for(
      std::string{align_model_table_group_node::get_user_visible_operator_name()})};

  return registry_.get_or_create_registry_entry(
      make_table_group_cache_key(activity_column, model, operator_context),
      create_align_model_tables{scope_, activity_column, case_column, model, add_telemetry_counter}, operator_context);
}
#endif

}  // namespace celonis::accelerator::operators::process::align_model
