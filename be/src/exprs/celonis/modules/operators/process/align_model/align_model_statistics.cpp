#include "align_model_statistics.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>

#include <cpml/exception.h>
#include <cpml/model/bpmn/utility.h>
#include <cpml/model/bpmn_graph.h>
#include <cpml/model/process_tree.h>
#include <ctl/assert.h>
#include <ctl/utils/time_utils.h>
#include <format/json/json.h>
#include <format/json/json_fwd.h>
#include <log/metadata.h>

namespace celonis::accelerator::operators::process::align_model {

namespace {

using duration_unit_for_logging_t = std::chrono::microseconds;

template <typename F, typename T, typename P = std::identity>
requires(
    /* Projection P must be invocable with type in the optional */
    std::invocable<P, typename std::remove_cvref_t<T>>&&
        /* Functor F must be invocable with return type of the projection */
        std::invocable<
            F, std::invoke_result_t<P, std::remove_cvref_t<T>>>) void invoke_with_if_has_value(F&& functor,
                                                                                               const std::optional<T>
                                                                                                   value,
                                                                                               P projection = {}) {
  if (value.has_value()) {
    functor(projection(*value));
  }
}

template <typename T, typename P = std::identity>
requires(
    /* Projection P must be invocable with type in the optional */
    std::invocable<P, typename std::remove_cvref_t<T>>) void add_if_has_value(format::json::json_object_t& json_object,
                                                                              const std::string& counter_name,
                                                                              const std::optional<T> value,
                                                                              P projection = {}) {
  invoke_with_if_has_value(
      [&json_object, &counter_name](const format::json::json_value_t& val) { json_object[counter_name] = val; }, value,
      projection);
}

void add_formatted_time(format::json::json_object_t& json, const std::string_view key, const auto& timing) {
  const auto callback{[&json](const std::string_view key, const size_t value) { json[std::string{key}] = value; }};
  ctl::format_and_add_duration<duration_unit_for_logging_t>(key, timing, callback);
}

[[nodiscard]] format::json::json_object_t make_json(const create_alignment_table_statistics& value) {
  format::json::json_object_t json{};
  const auto add_formatted_time_fn{
      [&json](const std::string_view key, const auto& timing) { add_formatted_time(json, key, timing); }};
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_create_table"), value.wall_time_create_table);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_model_vertex_column"),
                           value.wall_time_add_model_vertex_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_activity_label_column"),
                           value.wall_time_add_activity_label_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_move_type_column"),
                           value.wall_time_add_move_type_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_total"),
                           value.total_wall_time_create_alignment_table);
  return json;
}

[[nodiscard]] format::json::json_object_t make_json(const create_association_table_statistics& value) {
  format::json::json_object_t json{};
  const auto add_formatted_time_fn{
      [&json](const std::string_view key, const auto& timing) { add_formatted_time(json, key, timing); }};
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_create_table"), value.wall_time_create_table);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_association_column"),
                           value.wall_time_add_association_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_model_vertex_column"),
                           value.wall_time_add_model_vertex_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_activity_label_column"),
                           value.wall_time_add_activity_label_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_move_type_column"),
                           value.wall_time_add_move_type_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_total"),
                           value.total_wall_time_create_association_table);
  return json;
}

[[nodiscard]] format::json::json_object_t make_json(const create_edge_class_table_statistics& value) {
  format::json::json_object_t json{};
  const auto add_formatted_time_fn{
      [&json](const std::string_view key, const auto& timing) { add_formatted_time(json, key, timing); }};
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_create_table"), value.wall_time_create_table);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_edge_class_id_column"),
                           value.wall_time_add_edge_class_id_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_add_edge_class_type_column"),
                           value.wall_time_add_edge_class_type_column);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_total"),
                           value.total_wall_time_create_edge_class_table);
  return json;
}

[[nodiscard]] format::json::json_object_t make_json(const create_tables_and_add_columns_statistics& value) {
  format::json::json_object_t json_create_tables_and_add_columns_statistics{};
  const auto add_formatted_time_fn{
      [&json_create_tables_and_add_columns_statistics](const std::string_view key, const auto& timing) {
        add_formatted_time(json_create_tables_and_add_columns_statistics, key, timing);
      }};

  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_insert_dependencies"),
                           value.wall_time_insert_dependencies);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_register_alignment_to_activity_joins"),
                           value.wall_time_register_alignment_to_activity_joins);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_register_association_to_alignment_joins"),
                           value.wall_time_register_association_to_alignment_joins);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_register_association_to_edge_class_joins"),
                           value.wall_time_register_association_to_edge_class_joins);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_register_event_table_and_make_table_group"),
                           value.wall_time_register_event_table_and_make_table_group);

  json_create_tables_and_add_columns_statistics["create_alignment_table_statistics"] =
      make_json(value.create_alignment_table_stats);
  json_create_tables_and_add_columns_statistics["create_association_table_statistics"] =
      make_json(value.create_association_table_stats);
  json_create_tables_and_add_columns_statistics["create_edge_class_table_statistics"] =
      make_json(value.create_edge_class_table_stats);

  return json_create_tables_and_add_columns_statistics;
}

[[nodiscard]] format::json::json_object_t make_json(const inflation_statistics& value) {
  format::json::json_object_t json_inflation_statistics{};

  const auto add_formatted_time_fn{[&json_inflation_statistics](const std::string_view key, const auto& timing) {
    add_formatted_time(json_inflation_statistics, key, timing);
  }};

  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_compute_parallelization_blocks"),
                           value.wall_time_compute_parallelization_blocks);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_create_column_and_join_arrays"),
                           value.wall_time_create_column_and_join_arrays);
  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_create_data_columns_and_join_vectors"),
                           value.wall_time_create_data_columns_and_join_vectors);
  invoke_with_if_has_value(
      std::bind_front(add_formatted_time_fn, "time_create_tables_and_add_columns"),
      value.create_tables_and_add_columns_call_statistics.total_wall_time_create_tables_and_add_columns);

  invoke_with_if_has_value(std::bind_front(add_formatted_time_fn, "time_inflation_total"),
                           value.total_wall_time_inflation);

  json_inflation_statistics["create_tables_and_add_columns_statistics"] =
      make_json(value.create_tables_and_add_columns_call_statistics);
  return json_inflation_statistics;
}

}  // namespace

format::json::json_object_t to_json(const align_model_statistics& stats) {
  format::json::json_object_t alignment_metrics{};

  switch (stats.status) {
    case computation_status::FAILURE:
      alignment_metrics["computation_status"] = "FAILURE";
      break;
    case computation_status::SUCCESS:
      alignment_metrics["computation_status"] = "SUCCESS";
      break;
    default:
      ctl::assert_unreachable();
  }

  // General operators statistics
  {
    format::json::json_object_t general_stats{};
    add_if_has_value(general_stats, "bpmn_graph", stats.bpmn_graph, [](const cpml::model::bpmn_graph& bpmn_graph) {
      return cpml::model::bpmn::to_dot_pretty(bpmn_graph);
    });
    add_if_has_value(general_stats, "num_rows_eventlog", stats.num_rows_eventlog);
    // Output table metrics
    add_if_has_value(general_stats, "alignment_table_row_count", stats.alignment_table_row_count);
    add_if_has_value(general_stats, "association_table_row_count", stats.association_table_row_count);
    add_if_has_value(general_stats, "edge_class_table_row_count", stats.edge_class_table_row_count);
    alignment_metrics["general_statistics"] = std::move(general_stats);
  }

  // P1: Model transformations
  {
    format::json::json_object_t model_transformation_stats{};
    auto callable{std::bind_front(add_formatted_time<ctl::wall_time_duration>, std::ref(model_transformation_stats))};
    invoke_with_if_has_value(std::bind_front(callable, "time_proto_bpmn_to_bpmn_graph"),
                             stats.proto_bpmn_to_bpmn_graph);
    invoke_with_if_has_value(std::bind_front(callable, "time_bpmn_graph_to_petri_net"), stats.bpmn_graph_to_petri_net);
    alignment_metrics["model_transformation_statistics"] = std::move(model_transformation_stats);
  }

  // P2: Computing variants from the EL
  {
    format::json::json_object_t variant_computation_stats{};
    invoke_with_if_has_value(std::bind_front(add_formatted_time<ctl::wall_time_duration>,
                                             std::ref(variant_computation_stats), "time_variant_computation"),
                             stats.variant_computation);
    add_if_has_value(variant_computation_stats, "variant_count", stats.variant_count);
    alignment_metrics["variant_computation_statistics"] = std::move(variant_computation_stats);
  }

  // P3: Variant alignment algorithm execution
  ctl::wall_time_duration total_wall_time_alignment{};
  if (stats.alignment_stats.has_value()) {
    const auto& json_alignment_stats{stats.alignment_stats.value()};
    alignment_metrics["alignment_statistics"] = json_alignment_stats;
    if (static const format::json::json_key_t key{"wall_time_variant_alignment_total_us"};
        json_alignment_stats.contains(key)) {
      const auto& json_value{json_alignment_stats.at(key)};
      const auto json_value_as_str{json_value.to_string()};
      total_wall_time_alignment = ctl::wall_time_duration{duration_unit_for_logging_t{std::stol(json_value_as_str)}};
    } else {
      alignment_metrics["alignment_statistics"]["total_alignment_time_missing_error"] = "Not stored in JSON";
    }
  }

  // P4: Replay
  {
    format::json::json_object_t replay_stats{};
    invoke_with_if_has_value(
        std::bind_front(add_formatted_time<ctl::wall_time_duration>, std::ref(replay_stats), "time_variant_replay"),
        stats.time_variant_replay);
    alignment_metrics["replay_statistics"] = std::move(replay_stats);
  }

  // P5: Inflation (i.e., table creation)
  alignment_metrics["inflation_statistics"] = make_json(stats.inflation_stats);
  if (stats.status == computation_status::SUCCESS) {
    // N.B: If the computation status is successful, we expect all optional execution statistics fields to be set
    const auto total_execution_time{stats.proto_bpmn_to_bpmn_graph.value() + stats.bpmn_graph_to_petri_net.value() +
                                    stats.variant_computation.value() + total_wall_time_alignment +
                                    stats.time_variant_replay.value()};
    add_formatted_time(alignment_metrics, "time_align_model_operator_execution", total_execution_time);
  }

  return alignment_metrics;
}

}  // namespace celonis::accelerator::operators::process::align_model
