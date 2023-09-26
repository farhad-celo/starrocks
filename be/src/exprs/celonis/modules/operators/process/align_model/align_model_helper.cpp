#include "align_model_helper.h"

#include "common/status.h"
#include "common/statusor.h"
#include "exprs/celonis/result_table.h"
#include "exprs/celonis/variant.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/cube/ccmm/ccmm_manager.h"
#include "modules/cube/variant_trace_cache_manager.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table.h"
#include "modules/operators/process/align_model/align_model_table_group_node.h"
#include "modules/query/operators.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
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

namespace {

rapidjson::Value ConvertArray(const std::vector<std::string>& input, rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value result(rapidjson::kArrayType);
    for (const auto& element: input) {
        rapidjson::Value obj;
        obj = rapidjson::StringRef(element.c_str());
        result.PushBack(obj, allocator);
    }
    return result;
}

rapidjson::Value ConvertArray(const std::vector<int>& input, rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value result(rapidjson::kArrayType);
    for (const auto& element: input) {
        result.PushBack(element, allocator);
    }
    return result;
}

rapidjson::Value ConvertArray(const utils::nullable_vec_t<cel_int_t>& input,
                              rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value result(rapidjson::kArrayType);
    for (const auto& element: input) {
        if (element.is_null()) {
            result.PushBack(rapidjson::Value(rapidjson::Type::kNullType), allocator);
        } else {
            result.PushBack(element.get_or_throw(), allocator);
        }
    }
    return result;
}

} // namespace

std::string AlignModelHelper::json_string() {
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();
    d.SetObject();

    {
        rapidjson::Value alignment_schema(rapidjson::kObjectType);
        alignment_schema.AddMember("variant", "ARRAY<STRING>", allocator);
        alignment_schema.AddMember("model_vertex_id", "ARRAY<BIGINT>", allocator);
        alignment_schema.AddMember("vertex_label", "ARRAY<STRING>", allocator);
        alignment_schema.AddMember("move_type", "ARRAY<STRING>", allocator);
        alignment_schema.AddMember("activity_index", "ARRAY<BIGINT>", allocator);
        d.AddMember("alignment__SCHEMA", alignment_schema, allocator);

        rapidjson::Value alignment(rapidjson::kArrayType);
        const auto& variant = alignment_table_->column<std::vector<std::string>>("variant");
        const auto& model_vertex_id = alignment_table_->column<utils::nullable_vec_t<cel_int_t>>("model_vertex_id");
        const auto& vertex_label = alignment_table_->column<std::vector<std::string>>("vertex_label");
        const auto& move_type = alignment_table_->column<std::vector<std::string>>("move_type");
        const auto& activity_index = alignment_table_->column<std::vector<row_id>>("activity_index");
        for (int i = 0; i < alignment_table_->size(); i++) {
            rapidjson::Value row(rapidjson::kObjectType);
            row.AddMember("variant", ConvertArray(variant[i], allocator), allocator);
            row.AddMember("model_vertex_id", ConvertArray(model_vertex_id[i], allocator), allocator);
            row.AddMember("vertex_label", ConvertArray(vertex_label[i], allocator), allocator);
            row.AddMember("move_type", ConvertArray(move_type[i], allocator), allocator);
            row.AddMember("activity_index", ConvertArray(activity_index[i], allocator), allocator);
            alignment.PushBack(row, allocator);
        }
        d.AddMember("alignment", alignment, allocator);
    }

    {
        rapidjson::Value association_schema(rapidjson::kObjectType);
        association_schema.AddMember("variant", "ARRAY<STRING>", allocator);
        association_schema.AddMember("edge_class", "ARRAY<BIGINT>", allocator);
        association_schema.AddMember("alignment_index", "ARRAY<BIGINT>", allocator);
        d.AddMember("association__SCHEMA", association_schema, allocator);

        rapidjson::Value association(rapidjson::kArrayType);
        const auto& variant = association_table_->column<std::vector<std::string>>("variant");
        const auto& edge_class = association_table_->column<std::vector<row_id>>("edge_class");
        const auto& alignment_index = association_table_->column<std::vector<row_id>>("alignment_index");
        for (int i = 0; i < association_table_->size(); i++) {
            rapidjson::Value row(rapidjson::kObjectType);
            row.AddMember("variant", ConvertArray(variant[i], allocator), allocator);
            row.AddMember("edge_class", ConvertArray(edge_class[i], allocator), allocator);
            row.AddMember("alignment_index", ConvertArray(alignment_index[i], allocator), allocator);
            association.PushBack(row, allocator);
        }
        d.AddMember("association", association, allocator);
    }

    {
        rapidjson::Value edge_class_schema(rapidjson::kObjectType);
        edge_class_schema.AddMember("variant", "ARRAY<STRING>", allocator);
        edge_class_schema.AddMember("id", "ARRAY<BIGINT>", allocator);
        edge_class_schema.AddMember("type", "ARRAY<STRING>", allocator);
        d.AddMember("edge_class__SCHEMA", edge_class_schema, allocator);

        rapidjson::Value edge_class(rapidjson::kArrayType);
        const auto& variant = edge_class_table_->column<std::vector<std::string>>("variant");
        const auto& id = edge_class_table_->column<std::vector<row_id>>("id");
        const auto& type = edge_class_table_->column<std::vector<std::string>>("type");
        for (int i = 0; i < edge_class_table_->size(); i++) {
            rapidjson::Value row(rapidjson::kObjectType);
            row.AddMember("variant", ConvertArray(variant[i], allocator), allocator);
            row.AddMember("id", ConvertArray(id[i], allocator), allocator);
            row.AddMember("type", ConvertArray(type[i], allocator), allocator);
            edge_class.PushBack(row, allocator);
        }
        d.AddMember("edge_class", edge_class, allocator);
    }

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

Status AlignModelHelper::execute(const starrocks::VariantHashMap& variant_map,
                                 const starrocks::SliceHashMap& activity_map,
                                 const std::string& json_bpmn_model_description) {
    // Convert json bpmn model description to BpmnModelDescription protobuf.
    ASSIGN_OR_RETURN(auto bpmn_model_description, bpmn_builder::build(json_bpmn_model_description));

    // Convert variant_map and activitity_map to Saola event_table, case_table and activity_to_case_join.
    std::vector<std::string> activities;
    activities.reserve(activity_map.size() + 1);
    for (auto it = activity_map.begin(); it != activity_map.end(); it++) {
        if (it->second >= activities.size()) {
            activities.resize(it->second + 1);
        }
        activities[it->second] = it->first.to_string();
    }
    utils::nullable_vec_t<cel_int_t> case_column_data;
    utils::nullable_vec_t<cel_string_t> activity_column_data;
    utils::nullable_vec_t<cel_int_t> unique_object_ids;
    std::vector<row_id> activity_to_case_join_temp;
    unique_object_ids.reserve(variant_map.size());
    int object_id = 0;
    for (const auto& [variant, count] : variant_map) {
      object_id++;
      unique_object_ids.push_back(object_id);
      for (auto activity_id : variant.data) {
          case_column_data.push_back(object_id);
          activity_to_case_join_temp.push_back(object_id - 1);
          activity_column_data.push_back(activities[activity_id]);
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
            variant_map.size(), params.case_table_name, params.case_table_name, memory::management::no_swap(),
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
    alignment_table_ = std::move(tables.at(std::string(INTERNAL_ALIGNMENT_TABLE_NAME)));
    association_table_ = std::move(tables.at(std::string(INTERNAL_ASSOCIATION_TABLE_NAME)));
    edge_class_table_ = std::move(tables.at(std::string(INTERNAL_EDGE_CLASS_TABLE_NAME)));

    return Status::OK();
}

} // namespace celonis::accelerator::operators::process::align_model
