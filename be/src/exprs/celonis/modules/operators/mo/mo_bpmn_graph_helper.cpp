#include "mo_bpmn_graph_helper.h"

#include "common/status.h"
#include "common/statusor.h"
#include "modules/common/execution_context.h"
#include "modules/memory/const_abstract_column_ptrs_accessor.h"
#include "modules/operators/mo/mo_bpmn_graph_operator.h"
#include "modules/operators/process/inductive_miner/inductive_miner_statistics.h"
#include "modules/operators/process/inductive_miner/process_tree.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "utils/builders/column_builder.h"
#include "utils/nullable_pql_value.h"

using starrocks::Status;

namespace celonis::accelerator::operators::mo {

using process::process_tree;

namespace process_tree_builder {

void recalculate_counts(process_tree& pt) {
    std::visit(ctl::overloaded{
                       [&](process_tree::exclusive &p) {
                           p.recalculate_counts();
                       },
                       [&](process_tree::sequence &p) {
                           p.recalculate_counts();
                       },
                       [&](process_tree::parallel &p) {
                           p.recalculate_counts();
                       },
                       [&](process_tree::redo &p) {
                           p.recalculate_counts();
                       },
                       [&](auto & /*unused*/) {
                       }},
               pt.node);
}

StatusOr<std::tuple<process_tree, process::inductive_miner_statistics, memory::column_t>>
build(const std::string& process_tree_json, int id, const common::execution_context& parent_context) {
    const auto context{parent_context.create_sub_context("build_process_tree", {})};

    rapidjson::Document document;
    document.Parse(process_tree_json.c_str());
    if (document.HasParseError()) {
        std::stringstream error;
        error << "celonis_mo_bpmn_graph: Can't parse JSON process tree.";
        return Status::InvalidArgument(error.str());
    }

    if (!document.HasMember("vertex_properties")) {
        std::stringstream error;
        error << "celonis_mo_bpmn_graph: Process tree " << id << " does not contain 'vertex_properties'.";
        return Status::InvalidArgument(error.str());
    }
    if (!document.HasMember("edge_properties")) {
        std::stringstream error;
        error << "celonis_mo_bpmn_graph: Process tree " << id << " does not contain 'edge_properties'.";
        return Status::InvalidArgument(error.str());
    }
    if (!document.HasMember("statistics")) {
        std::stringstream error;
        error << "celonis_mo_bpmn_graph: Process tree " << id << " does not contain 'statistics'.";
        return Status::InvalidArgument(error.str());
    }

    const rapidjson::Value& vertex_values = document["vertex_properties"];
    const rapidjson::Value& edge_values = document["edge_properties"];
    if (vertex_values.Size() != edge_values.Size() + 1) {
        std::stringstream error;
        error << "celonis_mo_bpmn_graph: Process tree " << id << " is invalid. Table size mismatch.";
        return Status::InvalidArgument(error.str());
    }

    // Builds an activity column.
    size_t index = 0;
    std::vector<size_t> activity_indexes;
    activity_indexes.resize(vertex_values.Size());
    utils::nullable_vec_t<cel_string_t> activity_column_data;
    for (rapidjson::SizeType i = 0; i < vertex_values.Size(); ++i) {
        if (vertex_values[i]["process_tree_type"].GetInt() == 1) {
            activity_column_data.push_back(vertex_values[i]["activity"].GetString());
            activity_indexes[i] = index++;
        }
    }
    const auto activity_column{column_builder<cel_string_t>{}
            .name("ACTIVITY")
            .data(activity_column_data)
            .cache_key(fmt::format("ACTIVITY_TABLE_{}.ACTIVITY", id))
            .build()};
    const auto& col_ptrs{activity_column->get_column_pointers(context)};
    const memory::const_abstract_column_ptrs_accessor col_ptrs_ac{col_ptrs};

    // Builds a process tree.
    std::vector<process_tree> nodes;
    nodes.reserve(vertex_values.Size());
    for (rapidjson::SizeType i = 0; i < vertex_values.Size(); ++i) {
        auto process_tree_type = vertex_values[i]["process_tree_type"].GetInt();
        switch (process_tree_type) {
            case 0: {
                size_t object_count = vertex_values[i]["object_count"].GetInt64();
                nodes.emplace_back(process_tree{process_tree::tau{object_count}});
                break;
            }
            case 1: {
                row_id activity_id = col_ptrs_ac[activity_indexes[i]];
                size_t object_count = vertex_values[i]["object_count"].GetInt64();
                nodes.emplace_back(process_tree{process_tree::activity{activity_id, object_count}});
                break;
            }
            case 2:
                nodes.emplace_back(process_tree{process_tree::exclusive{}});
                break;
            case 3:
                nodes.emplace_back(process_tree{process_tree::sequence{}});
                break;
            case 4:
                nodes.emplace_back(process_tree{process_tree::parallel{}});
                break;
            case 5:
                nodes.emplace_back(process_tree{process_tree::redo{}});
                break;
            default:
                std::stringstream error;
                error << "celonis_mo_bpmn_graph: Process tree " << id << " has invalid process_tree_type "
                      << process_tree_type << ".";
                return Status::InvalidArgument(error.str());
        }
    }
    for (rapidjson::SizeType i = edge_values.Size(); i > 0; --i) {
        auto source_id = edge_values[i - 1]["edge_source_id"].GetInt();
        auto target_id = edge_values[i - 1]["edge_target_id"].GetInt();
        if (i != target_id) {
            std::stringstream error;
            error << "celonis_mo_bpmn_graph: Process tree " << id << " is invalid. Wrong edge_target_id.";
            return Status::InvalidArgument(error.str());
        }
        recalculate_counts(nodes.back());
        RETURN_IF_ERROR(std::visit(ctl::overloaded{
                                           [&](process_tree::exclusive& p) {
                                               p.children.insert(p.children.begin(), std::move(nodes.back()));
                                               return Status::OK();
                                           },
                                           [&](process_tree::sequence& p) {
                                               p.children.insert(p.children.begin(), std::move(nodes.back()));
                                               return Status::OK();
                                           },
                                           [&](process_tree::parallel& p) {
                                               p.children.insert(p.children.begin(), std::move(nodes.back()));
                                               return Status::OK();
                                           },
                                           [&](process_tree::redo& p) {
                                               p.children.insert(p.children.begin(), std::move(nodes.back()));
                                               return Status::OK();
                                           },
                                           [&](auto& /*unused*/) {
                                               std::stringstream error;
                                               error << "celonis_mo_bpmn_graph: Process tree " << id << " is invalid. "
                                                     << "edge_source_id with no parent type.";
                                               return Status::InvalidArgument(error.str());
                                           }},
                                   nodes[source_id].node));
        nodes.pop_back();
    }
    recalculate_counts(nodes[0]);

    // Gets statistics.
    const rapidjson::Value& statistics_values = document["statistics"];
    process::inductive_miner_statistics statistics;
    for (rapidjson::SizeType i = 0; i < statistics_values.Size(); ++i) {
        auto key = statistics_values[i]["key"].GetString();
        auto value = std::stoll(statistics_values[i]["value"].GetString());
        statistics.insert_or_assign(key, value);
    }
    return std::make_tuple(std::move(nodes[0]), std::move(statistics), std::move(activity_column));
}

} // namespace process_tree_builder

namespace tables_to_json_converter {

template<typename T>
void add_column(memory::column_t column, const char* column_name, std::vector<rapidjson::Value>& objs,
                rapidjson::Document::AllocatorType& allocator) {
    auto iterable = memory::to_iterable<T>(column);
    std::visit([&](const auto& iterable) {
                   int i = 0;
                   for (const std::optional<T> value: iterable) {
                       if (value.has_value()) {
                           if constexpr (std::is_same_v<T, cel_string_t>) {
                               objs[i].AddMember(rapidjson::StringRef(column_name),
                                                 rapidjson::Value().SetString(value.value(), allocator),
                                                 allocator);
                           } else {
                               // TODO(j.kim): May need to convert to string for some columns with int64_t.
                               objs[i].AddMember(rapidjson::StringRef(column_name), value.value(), allocator);
                           }
                       } else {
                           objs[i].AddMember(rapidjson::StringRef(column_name),
                                             rapidjson::Value(rapidjson::Type::kNullType), allocator);
                       }
                       ++i;
                   }
               },
               iterable);
}

void add_column(memory::table_t table, const char* column_name, std::vector<rapidjson::Value>& objs,
                rapidjson::Document::AllocatorType& allocator, const common::execution_context& context) {
    auto column_or_error = table->get_column_header_or_error_string(column_name, context);
    if (std::holds_alternative<std::string>(column_or_error)) {
        LOG(ERROR) << "Column with name [\"" << column_name << "\"] cannot be found on table [\"" << table->get_name()
                   << "\"].";
        return;
    }

    auto column = std::get<memory::column_t>(column_or_error);
    if (column->is_cel_string_type()) {
        add_column<cel_string_t>(column, column_name, objs, allocator);
    } else if (column->is_cel_int_type()) {
        add_column<cel_int_t>(column, column_name, objs, allocator);
    } else if (column->is_cel_float_type()) {
        add_column<cel_float_t>(column, column_name, objs, allocator);
    } else {
        LOG(ERROR) << "Not supported type of column with name [\"" << column_name << "\"] on table [\""
                   << table->get_name() << "\"].";
    }
}

rapidjson::Value convert_table(memory::table_t table, const std::vector<const char*>& columns,
                               rapidjson::Document::AllocatorType& allocator,
                               const common::execution_context& context) {
    rapidjson::Value result(rapidjson::kArrayType);
    int num_rows = table->get_rows();
    std::vector<rapidjson::Value> objs;
    objs.reserve(num_rows);
    for (int i = 0; i < num_rows; ++i) {
        objs.emplace_back(rapidjson::kObjectType);
    }
    for (const char* column : columns) {
        add_column(table, column, objs, allocator, context);
    }
    for (int i = 0; i < num_rows; ++i) {
        result.PushBack(objs[i], allocator);
    }
    return result;
}

std::string convert(const process::bpmn::bpmn_tables& bpmn_tables, const common::execution_context& context) {
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();
    d.SetObject();

    d.AddMember("bpmn_edges",
                convert_table(bpmn_tables.bpmn_edges,
                              {"SOURCE_ID", "TARGET_ID", "OBJECT_ID", "OBJECT_COUNT"},
                              allocator, context),
                allocator);
    d.AddMember("bpmn_nodes",
                convert_table(bpmn_tables.bpmn_nodes,
                              {"NODE_ID", "NODE_TYPE"},
                              allocator, context),
                allocator);
    d.AddMember("bpmn_activities",
                convert_table(bpmn_tables.bpmn_activities,
                              {"NODE_ID", "ACTIVITY_NAME"},
                              allocator, context),
                allocator);
    d.AddMember("bpmn_model_descriptions",
                convert_table(bpmn_tables.bpmn_model_descriptions,
                              {"OBJECT_ID", "BPMN_MODEL_DESCRIPTION"},
                              allocator, context),
                allocator);
    d.AddMember("bpmn_blocks",
                convert_table(bpmn_tables.bpmn_blocks,
                              {"BLOCK_ID", "PARENT_BLOCK_ID", "OBJECT_ID", "BLOCK_TYPE"},
                              allocator, context),
                allocator);
    d.AddMember("bpmn_nodes_to_blocks",
                convert_table(bpmn_tables.bpmn_nodes_to_blocks,
                              {"NODE_ID", "BLOCK_ID"},
                              allocator, context),
                allocator);

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

} // namespace tables_to_json_converter

StatusOr<std::string> MoBpmnGraphHelper::execute(const std::vector<std::string>& process_trees_json) {
    common::execution_context context;
    std::vector<process::process_tree> process_trees;
    std::vector<process::inductive_miner_statistics> statistics;
    std::vector<memory::column_t> activity_columns;
    for (int i = 0; i < process_trees_json.size(); ++i) {
        ASSIGN_OR_RETURN(auto results, process_tree_builder::build(process_trees_json[i], i, context));
        process_trees.push_back(std::move(std::get<0>(results)));
        statistics.push_back(std::move(std::get<1>(results)));
        activity_columns.push_back(std::move(std::get<2>(results)));
    }
    auto mo_bpmn_graph_op = mo_bpmn_graph_operator(std::move(process_trees), std::move(statistics),
                                                   std::move(activity_columns), context);
    auto result = mo_bpmn_graph_op.compute();
    return tables_to_json_converter::convert(result.tables, context);
}

} // namespace celonis::accelerator::operators::mo