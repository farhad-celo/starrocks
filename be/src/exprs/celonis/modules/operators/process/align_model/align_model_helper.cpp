#include "align_model_helper.h"

#include "common/status.h"
#include "common/statusor.h"
#include "exprs/celonis/result_table.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/cube/ccmm/ccmm_manager.h"
#include "modules/cube/variant_trace_cache_manager.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table.h"
#include "modules/operators/process/align_model/align_model_table_group_node.h"
#include "modules/query/operators.pb.h"
#include "rapidjson/document.h"
#include "utils/builders/column_builder.h"
#include "utils/nullable_pql_value.h"

using starrocks::celonis::ResultColumn;
using starrocks::StatusOr;

namespace celonis::accelerator::operators::process::align_model {

struct eventlog_params {
    std::string case_table_name{"CASE_TABLE"};
    std::string activity_table_name{"ACTIVITY_TABLE"};
    std::string case_col_name{cube::ccmm::OBJECT_ID_COLUMN_KEY};
    std::string activity_col_name{cube::ccmm::ACTIVITY_COLUMN_KEY};
    std::string timestamp_col_name{cube::ccmm::TIMESTAMP_COLUMN_KEY};
    bool is_default_eventlog{true};
};

namespace bpmn_builder {

StatusOr<BpmnModelDescription> build(const std::string& json_bpmn_model_description) {
    BpmnModelDescription bpmn;

    rapidjson::Document document;
    document.Parse(json_bpmn_model_description.c_str());
    if (document.HasParseError()) {
        std::stringstream error;
        error << "celonis_align_model: Can't parse JSON bpmn model description.";
        return Status::InvalidArgument(error.str());
    }

    if (!document.HasMember("nodes")) {
        std::stringstream error;
        error << "celonis_align_model: Your model does not contain 'nodes'.";
        return Status::InvalidArgument(error.str());
    }
    const rapidjson::Value& nodes_values = document["nodes"];
    for (rapidjson::SizeType i = 0; i < nodes_values.Size(); ++i) {
        auto node = bpmn.add_nodes();
        node->set_node_id(nodes_values[i]["node_id"].GetInt64());
        node->set_node_type(
                static_cast<BpmnModelDescription_BpmnNode_BpmnNodeType>(nodes_values[i]["node_type"].GetInt()));
        if (node->node_type() == BpmnModelDescription_BpmnNode_BpmnNodeType_TASK) {
            node->set_task_name(nodes_values[i]["task_name"].GetString());
        }
    }

    if (!document.HasMember("edges")) {
        std::stringstream error;
        error << "celonis_align_model: Your model does not contain 'edges'.";
        return Status::InvalidArgument(error.str());
    }
    const rapidjson::Value& edges_values = document["edges"];
    for (rapidjson::SizeType i = 0; i < edges_values.Size(); ++i) {
        auto node = bpmn.add_edges();
        node->set_from(edges_values[i]["from"].GetInt64());
        node->set_to(edges_values[i]["to"].GetInt64());
    }

    if (!document.HasMember("cache_key")) {
        std::stringstream error;
        error << "celonis_align_model: Your model does not contain 'cache_key'.";
        return Status::InvalidArgument(error.str());
    }
    bpmn.set_cache_key(document["cache_key"].GetString());

    return bpmn;
}

} // namespace bpmn_builder

Status AlignModelHelper::execute(const std::vector<std::vector<std::string>>& variants,
                                 const std::string& json_bpmn_model_description) {
    // Convert json bpmn model description to BpmnModelDescription protobuf.
    ASSIGN_OR_RETURN(auto bpmn_model_description, bpmn_builder::build(json_bpmn_model_description));

    // Convert variant_map and activitity_map to Saola event_table, case_table and activity_to_case_join.
    utils::nullable_vec_t<cel_int_t> case_column_data;
    utils::nullable_vec_t<cel_string_t> activity_column_data;
    utils::nullable_vec_t<cel_int_t> unique_object_ids;
    std::vector<row_id> activity_to_case_join_temp;
    unique_object_ids.reserve(variants.size());
    int object_id = 0;
    for (const auto& variant : variants) {
      object_id++;
      unique_object_ids.push_back(object_id);
      for (auto activity : variant) {
          case_column_data.push_back(object_id);
          activity_to_case_join_temp.push_back(object_id - 1);
          activity_column_data.push_back(activity);
      }
    }
    auto activity_to_case_join = memory::join_projection_vector_t{ctl::make_shared_static_array<row_id>(
            activity_to_case_join_temp, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

    eventlog_params params;
    memory::table_t event_table{std::make_shared<memory::table>(
            case_column_data.size(), params.activity_table_name, params.activity_table_name,
            memory::management::no_swap(), memory::table_meta_data::make_for_query_scope_aggregation_table(),
            memory::user_visible_table_name{params.activity_table_name}, memory::MAX_TABLE_ROW_LIMIT)};

    const auto case_id_column{column_builder<cel_int_t>{}
                                      .owner(event_table.get())
                                      .name(params.case_col_name)
                                      .data(case_column_data)
                                      .cache_key(fmt::format("{}.{}", params.activity_table_name, params.case_col_name))
                                      .build()};

    const auto activity_column{column_builder<cel_string_t>{}
                                       .owner(event_table.get())
                                       .name(params.activity_col_name)
                                       .data(activity_column_data)
                                       .cache_key(fmt::format("{}.{}", params.activity_table_name,
                                                              params.activity_col_name))
                                       .build()};

    event_table->add_existing_column(case_id_column, memory::MAX_TABLE_ROW_LIMIT);
    event_table->add_existing_column(activity_column, memory::MAX_TABLE_ROW_LIMIT);

    memory::table_t case_table{std::make_shared<memory::table>(
            variants.size(), params.case_table_name, params.case_table_name, memory::management::no_swap(),
            memory::table_meta_data::make_for_query_scope_aggregation_table(),
            memory::user_visible_table_name{params.case_table_name}, memory::MAX_TABLE_ROW_LIMIT)};

    const auto id_column{column_builder<cel_int_t>{}
            .owner(case_table.get())
            .name(cube::ccmm::OBJECT_ID_COLUMN_KEY)
            .data(unique_object_ids)
            .cache_key(fmt::format("{}.{}", params.case_table_name, params.case_col_name))
            .build()};

    case_table->add_existing_column(id_column, memory::MAX_TABLE_ROW_LIMIT);

    cube::variant_trace_cache_manager variant_trace_cache_manager(memory::management::no_swap());

    auto align_model = align_model::create_align_model_tables{
        activity_column, case_id_column, case_table, activity_to_case_join, &variant_trace_cache_manager,
        bpmn_model_description};

    common::execution_context context;
    auto tables = align_model.operate(context);
    result_table_ = std::move(tables.at("align_model"));

    return Status::OK();
}

} // namespace celonis::accelerator::operators::process::align_model
