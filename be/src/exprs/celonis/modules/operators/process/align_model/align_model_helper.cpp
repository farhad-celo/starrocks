#include "align_model_helper.h"

#include <algorithm>

#include <fmt/ostream.h>
#include <google/protobuf/util/json_util.h>

#include <cpml/exception.h>
#include <ctl/assert.h>
#include <ctl/conversion.h>

#include "common/status.h"
#include "exprs/celonis/result_table.h"
#include "exprs/celonis/utils/exception_remapping.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/operators/process/align_model/align_model_table_group_node_settings.h"
#include "modules/operators/process/align_model/create_align_model_tables.h"
#include "modules/query/operators.pb.h"
#include "utils/builders/column_builder.h"
#include "utils/nullable_pql_value.h"

namespace celonis::accelerator::operators::process::align_model {

// TODO(n.weber): CPL-10401 - Deprecated for removal
constexpr const char* ACTIVITY_COLUMN_KEY{"ACTIVITY"};
// TODO(n.weber): CPL-10401 - This should only be 'ID' in the future
constexpr const char* OBJECT_ID_COLUMN_KEY{"OBJECT_ID"};

struct eventlog_params {
  std::string case_table_name{"CASE_TABLE"};
  std::string activity_table_name{"ACTIVITY_TABLE"};
  std::string case_col_name{OBJECT_ID_COLUMN_KEY};
  std::string activity_col_name{ACTIVITY_COLUMN_KEY};
};

align_model_version to_saola_version(AlignModelHelper::celostar_align_model_version v) {
  switch (v) {
    case AlignModelHelper::celostar_align_model_version::V1:
      return align_model_version::V1;
    case AlignModelHelper::celostar_align_model_version::V2:
      return align_model_version::V2;
    case AlignModelHelper::celostar_align_model_version::V3:
      return align_model_version::V3;
    default:
      ctl::assert_unreachable();
  }
}

starrocks::StatusOr<starrocks::celonis::bpmn_model_description> AlignModelHelper::parse_bpmn_model_description(
    const std::string& json_bpmn_model_description) {
  // Convert json bpmn model description to BpmnModelDescription protobuf.
  BpmnModelDescription bpmn_model_description;
  auto status = google::protobuf::util::JsonStringToMessage(json_bpmn_model_description, &bpmn_model_description);
  if (!status.ok()) {
    return Status::InvalidArgument(
        fmt::format("celonis_align_model: Invalid JSON bpmn model description. {}", status.error_message()));
  }
  for (const auto& node : bpmn_model_description.nodes()) {
    const bool is_task = node.node_type() == BpmnModelDescription::BpmnNode::TASK;
    if (is_task != node.has_task_name()) {
      return Status::InvalidArgument(fmt::format(
          "celonis_align_model: Invalid BPMN model description. Node {} has an invalid task name.", node.node_id()));
    }
  }

  return starrocks::celonis::bpmn_model_description::from_proto(bpmn_model_description);
}

Status AlignModelHelper::execute(const traces_t& deduped_traces, const std::string& json_bpmn_model_description,
                                 celostar_align_model_version version) {
  auto bpmn_model_description = parse_bpmn_model_description(json_bpmn_model_description);
  if (!bpmn_model_description.ok()) {
    return bpmn_model_description.status();
  }

  return execute(deduped_traces, bpmn_model_description.value(), version);
}

Status AlignModelHelper::execute(const traces_t& deduped_traces,
                                 const starrocks::celonis::bpmn_model_description& bpmn_model_description,
                                 celostar_align_model_version version) {
  const auto output_projection = version == celostar_align_model_version::V3
                                     ? v2::create_alignment_output_projection::all_fields()
                                     : v2::create_alignment_output_projection::all_fields_v1();
  return execute(deduped_traces, bpmn_model_description, version, output_projection);
}

Status AlignModelHelper::execute(const traces_t& deduped_traces,
                                 const starrocks::celonis::bpmn_model_description& bpmn_model_description,
                                 celostar_align_model_version version,
                                 const v2::create_alignment_output_projection& output_projection) {
  auto settings{align_model_table_group_node_settings::builder{}.set_version(to_saola_version(version)).build()};
  const auto effective_output_projection = version == celostar_align_model_version::V1
                                               ? v2::create_alignment_output_projection::all_fields_v1()
                                               : output_projection;

  // Convert variant_map and activity_map to Saola event_table, case_table and activity_to_case_join.
  utils::nullable_vec_t<cel_int_t> case_column_data;
  utils::nullable_vec_t<cel_string_t> activity_column_data;
  std::vector<row_id> activity_to_case_join_temp;
  int object_id = 0;
  std::ranges::for_each(std::move(deduped_traces), [&](auto& current_trace) {
    object_id++;
    std::ranges::for_each(std::move(current_trace), [&](auto& activity) {
      case_column_data.emplace_back(object_id);
      activity_to_case_join_temp.push_back(object_id - 1);
      if (activity.has_value()) {
        activity_column_data.emplace_back(std::move(activity).value());
      } else {
        activity_column_data.emplace_back(cel_null_v);
      }
    });
  });
  auto activity_to_case_join = memory::join_projection_vector_t{
      ctl::make_shared_static_array<row_id>(activity_to_case_join_temp, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  eventlog_params params;

  const auto case_id_column{column_builder<cel_int_t>{}
                                .with_table_config(memory::table_config::from_name_only(params.activity_table_name))
                                .name(params.case_col_name)
                                .data(case_column_data)
                                .cache_key(fmt::format("{}.{}", params.activity_table_name, params.case_col_name))
                                .build()};

  const auto activity_column{column_builder<cel_string_t>{}
                                 .with_table_config(memory::table_config::from_name_only(params.activity_table_name))
                                 .name(params.activity_col_name)
                                 .data(activity_column_data)
                                 .cache_key(fmt::format("{}.{}", params.activity_table_name, params.activity_col_name))
                                 .build()};

  common::runtime_assert(case_id_column->get_row_count() == activity_column->get_row_count(),
                         "celonis_align_model: Case column and activity column must have the same row count.");

  // After some investigation, it was found that Celostar-SR never really required the memory::table data structure.
  // The only table usage was in the code below. When the table usage was followed, it was found that the only thing
  // called on the case_table was 'get_rows()' that returns the row count of the table. It was implemented as:
  // - If no column exists in the table, return its default row count (given to the ctor)
  // - If a column exists in the table, use the first column and return its row count (column::get_row_count(...))
  // For the table row count arg in the ctor we used 'deduped_traces.size()' and for the column row count we used
  // 'unique_object_ids.size()'. Since both sizes were equal, it is safe to simply directly pass around this size
  // instead of constructing a memory::table just for this purpose.
  // Note: The table row count was only accessed during the variant data structure computation in string_aggregation.h
  // 'compute_variant_row_ids'. Further proof that the table was not actually required but only its size can be found in
  // the string_aggregation.h alternative function interface compute_variant_row_ids where only a 'num_case_rows' arg
  // was passed and no (case table) at all.
  const row_id case_table_row_count{ctl::cast<row_id>(deduped_traces.size())};

  auto align_model = align_model::create_align_model_tables{
      activity_column,        case_id_column, case_table_row_count,       activity_to_case_join,
      bpmn_model_description, settings,       effective_output_projection};

  return starrocks::celonis::execute_and_return_status(
      [&] {
        common::execution_context context{};
        auto tables{align_model(context)};
        result_table_ = std::move(tables.at("align_model"));
      },
      get_user_visible_operator_name(settings.get_version()));
}

}  // namespace celonis::accelerator::operators::process::align_model
