#pragma once

#include <cpml/model/bpmn_graph.h>
#include <ctl/time.h>

#include "legacy_embedded_format/json/json_fwd.h"
#include "modules/operators/process/align_model/align_model_types.h"

namespace celonis::accelerator::operators::process::align_model {

/** Statistics for the phases in the sub-call 'create_tables_and_add_columns' of the inflation operation */
struct create_alignment_table_statistics {
  std::optional<ctl::wall_time_duration> wall_time_create_table{};
  std::optional<ctl::wall_time_duration> wall_time_add_model_vertex_column{};
  std::optional<ctl::wall_time_duration> wall_time_add_activity_label_column{};
  std::optional<ctl::wall_time_duration> wall_time_add_move_type_column{};
  std::optional<ctl::wall_time_duration> wall_time_add_deviation_category_column{};
  std::optional<ctl::wall_time_duration> total_wall_time_create_alignment_table{};
};

struct create_association_table_statistics {
  std::optional<ctl::wall_time_duration> wall_time_create_table{};
  std::optional<ctl::wall_time_duration> wall_time_add_association_column{};
  std::optional<ctl::wall_time_duration> wall_time_add_model_vertex_column{};
  std::optional<ctl::wall_time_duration> wall_time_add_activity_label_column{};
  std::optional<ctl::wall_time_duration> wall_time_add_move_type_column{};
  std::optional<ctl::wall_time_duration> total_wall_time_create_association_table{};
};

struct create_edge_class_table_statistics {
  std::optional<ctl::wall_time_duration> wall_time_create_table{};
  std::optional<ctl::wall_time_duration> wall_time_add_edge_class_id_column{};
  std::optional<ctl::wall_time_duration> wall_time_add_edge_class_type_column{};
  std::optional<ctl::wall_time_duration> total_wall_time_create_edge_class_table{};
};

/** Statistics for the phases in the sub-call 'create_tables_and_add_columns' of the inflation operation */
struct create_tables_and_add_columns_statistics {
  create_alignment_table_statistics create_alignment_table_stats{};
  create_association_table_statistics create_association_table_stats{};
  create_edge_class_table_statistics create_edge_class_table_stats{};
  std::optional<ctl::wall_time_duration> wall_time_insert_dependencies{};
  std::optional<ctl::wall_time_duration> wall_time_register_alignment_to_activity_joins{};
  std::optional<ctl::wall_time_duration> wall_time_register_association_to_alignment_joins{};
  std::optional<ctl::wall_time_duration> wall_time_register_association_to_edge_class_joins{};
  std::optional<ctl::wall_time_duration> wall_time_register_event_table_and_make_table_group{};
  std::optional<ctl::wall_time_duration> total_wall_time_create_tables_and_add_columns{};
};

/** Statistics for the phases in the 'inflation' operation */
struct inflation_statistics {
  std::optional<ctl::wall_time_duration> wall_time_compute_parallelization_blocks{};
  std::optional<ctl::wall_time_duration> wall_time_create_column_and_join_arrays{};
  std::optional<ctl::wall_time_duration> wall_time_create_data_columns_and_join_vectors{};
  create_tables_and_add_columns_statistics create_tables_and_add_columns_call_statistics{};
  std::optional<ctl::wall_time_duration> total_wall_time_inflation{};
};

enum class computation_status { FAILURE, SUCCESS };

struct align_model_statistics {
  computation_status status{computation_status::FAILURE};
  // Output tables metrics
  std::optional<size_t> alignment_table_row_count{};
  std::optional<size_t> association_table_row_count{};
  std::optional<size_t> edge_class_table_row_count{};

  std::optional<ctl::wall_time_duration> proto_bpmn_to_bpmn_graph{};
  std::optional<ctl::wall_time_duration> bpmn_graph_to_petri_net{};
  std::optional<size_t> variant_count{};
  std::optional<ctl::wall_time_duration> variant_computation{};

  std::optional<ctl::wall_time_duration> time_variant_replay{};
  std::optional<ctl::wall_time_duration> time_deviation_categories{};

  std::optional<format::json::json_object_t> alignment_stats{};
  inflation_statistics inflation_stats{};

  std::optional<cpml::model::bpmn_graph> bpmn_graph{};
  std::optional<row_id> num_rows_eventlog{};
};

[[nodiscard]] format::json::json_object_t to_json(const align_model_statistics& stats);

}  // namespace celonis::accelerator::operators::process::align_model
