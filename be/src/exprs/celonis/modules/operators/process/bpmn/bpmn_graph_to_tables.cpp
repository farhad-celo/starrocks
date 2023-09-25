#include "bpmn_graph_to_tables.h"

#include <vector>

#include "modules/memory/dictionary.h"
#include "modules/memory/table.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

memory::table_t build_bpmn_edges_table(const bpmn::bpmn_graph& graph, memory::table_row_limit_t table_row_limit,
                                       const common::execution_context& operator_context) {
  const auto& bpmn_edges{graph.get_edges()};
  const row_id size{static_cast<row_id>(bpmn_edges.size())};

  memory::table_t bpmn_edges_table{memory::table::create_query_scope_table(size, "bpmn_edges")};

  auto source_id_data{ctl::make_static_array_for_overwrite<cel_int_t>(size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};
  auto target_id_data{ctl::make_static_array_for_overwrite<cel_int_t>(size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};
  auto object_id_data{ctl::make_static_array_for_overwrite<cel_int_t>(size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};
  auto object_count_data{ctl::make_static_array_for_overwrite<cel_int_t>(size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};

  row_id i{0};
  for (const auto& edge : bpmn_edges) {
    source_id_data[i] = static_cast<cel_int_t>(edge.get_source_id());
    target_id_data[i] = static_cast<cel_int_t>(edge.get_target_id());
    object_id_data[i] = static_cast<cel_int_t>(edge.get_object_id());
    object_count_data[i] = static_cast<cel_int_t>(edge.get_count());
    ++i;
  }

  bpmn_edges_table->add_column<cel_int_t>(memory::col_name{"SOURCE_ID"}, memory::col_id{"SOURCE_ID"},
                                          std::move(source_id_data), memory::create_null_flags(size, operator_context),
                                          memory::column_processing_state{}, table_row_limit);
  bpmn_edges_table->add_column<cel_int_t>(memory::col_name{"TARGET_ID"}, memory::col_id{"TARGET_ID"},
                                          std::move(target_id_data), memory::create_null_flags(size, operator_context),
                                          memory::column_processing_state{}, table_row_limit);
  bpmn_edges_table->add_column<cel_int_t>(memory::col_name{"OBJECT_ID"}, memory::col_id{"OBJECT_ID"},
                                          std::move(object_id_data), memory::create_null_flags(size, operator_context),
                                          memory::column_processing_state{}, table_row_limit);
  bpmn_edges_table->add_column<cel_int_t>(
      memory::col_name{"OBJECT_COUNT"}, memory::col_id{"OBJECT_COUNT"}, std::move(object_count_data),
      memory::create_null_flags(size, operator_context), memory::column_processing_state{}, table_row_limit);

  return bpmn_edges_table;
}

memory::table_t build_bpmn_nodes_table(const bpmn::bpmn_graph& graph, memory::table_row_limit_t table_row_limit,
                                       const common::execution_context& operator_context) {
  const auto& bpmn_nodes{graph.get_vertices()};
  const row_id size{static_cast<row_id>(bpmn_nodes.size())};

  memory::table_t bpmn_nodes_table{memory::table::create_query_scope_table(size, "bpmn_nodes")};

  auto node_id_data{ctl::make_static_array_for_overwrite<cel_int_t>(size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};
  auto node_type_data{ctl::make_static_array_for_overwrite<cel_int_t>(size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};

  for (int i{0}; const auto& [_, vertex] : bpmn_nodes) {
    node_id_data[i] = static_cast<cel_int_t>(vertex.get_vertex_id());
    node_type_data[i] = bpmn::convert_vertex_type_to_int(vertex.get_vertex_type());
    i++;
  }

  bpmn_nodes_table->add_column<cel_int_t>(memory::col_name{"NODE_ID"}, memory::col_id{"NODE_ID"},
                                          std::move(node_id_data), memory::create_null_flags(size, operator_context),
                                          memory::column_processing_state{}, table_row_limit);
  bpmn_nodes_table->add_column<cel_int_t>(memory::col_name{"NODE_TYPE"}, memory::col_id{"NODE_TYPE"},
                                          std::move(node_type_data), memory::create_null_flags(size, operator_context),
                                          memory::column_processing_state{}, table_row_limit);

  return bpmn_nodes_table;
}

/*
 * Returns function that gets a mapping of indices to dict row ids and sets the given column pointers to the dict rid
 * for each index.
 */
auto exec_dict_mapper(const ctl::static_array<row_id>& dict_rid_mapping) {
  return [&dict_rid_mapping](const auto& t) {
    auto output_col_ptrs_ac{std::get<0>(t).get_data()};

    std::copy(dict_rid_mapping.begin(), dict_rid_mapping.end(), output_col_ptrs_ac.get());
  };
}

memory::table_t build_bpmn_activities(const bpmn_graph& graph, const memory::dictionary_t& activity_dict,
                                      memory::table_row_limit_t table_row_limit,
                                      const common::execution_context& context) {
  std::vector<bpmn::vertex> bpmn_nodes{};
  std::transform(graph.get_vertices().begin(), graph.get_vertices().end(), std::back_inserter(bpmn_nodes),
                 [](const auto& pair) { return pair.second; });
  std::vector<bpmn::vertex> bpmn_activity_nodes{};
  std::copy_if(bpmn_nodes.begin(), bpmn_nodes.end(), std::back_inserter(bpmn_activity_nodes),
               [](const bpmn::vertex& vertex) { return std::holds_alternative<bpmn::task>(vertex.get_vertex_type()); });
  const row_id size{static_cast<row_id>(bpmn_activity_nodes.size())};

  auto node_id_data{ctl::make_static_array_for_overwrite<cel_int_t>(size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};

  auto activity_name_dict_mapping{
      ctl::make_static_array_for_overwrite<row_id>(size, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  for (row_id i{0}; i < size; i++) {
    node_id_data[i] = static_cast<cel_int_t>(bpmn_activity_nodes[i].get_vertex_id());
    activity_name_dict_mapping[i] = std::get<bpmn::task>(bpmn_activity_nodes[i].get_vertex_type()).activity_id;
  }

  // create column pointers and map them to dictionary entries
  auto activity_name_output_column_pointer = memory::create_raw_column_pointer(
      static_cast<row_id>(bpmn_activity_nodes.size()), activity_dict->get_size(), memory::zero_init_t{false}, context);

  memory::cast_execute_column_pointers(exec_dict_mapper(activity_name_dict_mapping),
                                       *activity_name_output_column_pointer);

  memory::table_t bpmn_activities_table{memory::table::create_query_scope_table(size, "bpmn_activities")};

  bpmn_activities_table->add_column<cel_int_t>(memory::col_name{"NODE_ID"}, memory::col_id{"NODE_ID"},
                                               std::move(node_id_data), memory::create_null_flags(size, context),
                                               memory::column_processing_state{}, table_row_limit);
  bpmn_activities_table->add_column_with_dictified_data(
      cel_string, memory::col_name{"ACTIVITY_NAME"}, memory::col_id{"ACTIVITY_NAME"}, memory::col_cache_key{""},
      memory::create_tmp_column_pointers(activity_name_output_column_pointer), activity_dict, table_row_limit);

  return bpmn_activities_table;
}

memory::table_t build_coverage(const std::vector<row_id>& coverage, memory::table_row_limit_t table_row_limit,
                               const common::execution_context& operator_context) {
  memory::table_t coverage_table{
      memory::table::create_query_scope_table(ctl::cast<row_id>(coverage.size()), "bpmn_coverage")};
  auto coverage_data{
      ctl::make_static_array_for_overwrite<cel_int_t>(coverage.size(), ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};
  std::copy(coverage.begin(), coverage.end(), coverage_data.get());
  coverage_table->add_column<cel_int_t>(
      memory::col_name{"OBJECT_COUNT"}, memory::col_id{"OBJECT_COUNT"}, std::move(coverage_data),
      memory::create_null_flags(coverage.size(), operator_context), memory::column_processing_state{}, table_row_limit);
  return coverage_table;
}

struct edges_and_unique_vertices {
  // We use sorted sets here such that the order in which vertices and edges appear in the model description is
  // consistent.
  std::set<edge> edges{};
  std::set<vertex> vertices{};
};

using edges_and_vertices_per_object_t = std::unordered_map<object_id, edges_and_unique_vertices>;

[[nodiscard]] edges_and_vertices_per_object_t split_global_model_per_object(const bpmn_graph& global_model) {
  edges_and_vertices_per_object_t edges_and_vertices_per_object{};

  // Add all edges and vertices to their respective object ids in the maps
  for ([[maybe_unused]] const auto& edge : global_model.get_edges()) {
    const auto object_id{edge.get_object_id()};
    auto& [edges, vertices]{edges_and_vertices_per_object[object_id]};

    edges.insert(edge);

    // This does not insert if the vertex was already present in the set
    vertices.insert(vertex{edge.get_source_id(), global_model.get_vertex(edge.get_source_id()).get_vertex_type()});
    vertices.insert(vertex{edge.get_target_id(), global_model.get_vertex(edge.get_target_id()).get_vertex_type()});
  }

  return edges_and_vertices_per_object;
}

[[nodiscard]] std::string construct_model_description(const edges_and_unique_vertices& edges_and_vertices,
                                                      const memory::dictionary_t& activity_dict) {
  std::ostringstream model_description_strm{};

  const auto vertex_formatter{[&](const vertex& vertex) {
    const auto& vertex_type{vertex.get_vertex_type()};
    const auto optional_activity_name{
        bpmn::is_task(vertex_type)
            ? fmt::format(" '{}'", activity_dict->get_string_value(std::get<bpmn::task>(vertex_type).activity_id))
            : ""};

    return fmt::format("[{} {}{}]", vertex.get_vertex_id(), bpmn::to_string(vertex_type), optional_activity_name);
  }};

  const auto edge_formatter{
      [&](const edge& edge) { return fmt::format("[{} {}]", edge.get_source_id(), edge.get_target_id()); }};

  model_description_strm << "[";
  for (const auto& vertex : edges_and_vertices.vertices) {
    model_description_strm << vertex_formatter(vertex);
  }
  model_description_strm << "],[";
  for (const auto& edge : edges_and_vertices.edges) {
    model_description_strm << edge_formatter(edge);
  }
  model_description_strm << "]";

  return model_description_strm.str();
}

[[nodiscard]] memory::table_t build_bpmn_model_descriptions(const bpmn_graph& graph,
                                                            const memory::dictionary_t& activity_dict,
                                                            memory::table_row_limit_t table_row_limit,
                                                            const common::execution_context& operator_context) {
  const auto edges_and_vertices_per_object{split_global_model_per_object(graph)};
  const auto num_objects{static_cast<row_id>(edges_and_vertices_per_object.size())};

  // We cannot write the model_descriptions to to a string buffer directly as we do not know the total size
  // We use a sorted map such that the object ids appear in consecutive order in the result column
  std::map<object_id, std::string> model_descriptions_per_object{};
  size_t total_model_descriptions_size{0};

  for (const auto& [object_id, edges_and_vertices] : edges_and_vertices_per_object) {
    auto model_description{construct_model_description(edges_and_vertices, activity_dict)};

    total_model_descriptions_size += model_description.size() + 1;  // for string null-termination
    model_descriptions_per_object.emplace(object_id, std::move(model_description));
  }

  // Now that we know the total size of the model descriptions, we can allocate the arrays
  auto object_id_data{ctl::make_static_array_for_overwrite<cel_int_t>(num_objects, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};
  auto model_description_ptrs{
      ctl::make_static_array_for_overwrite<cel_string_t>(num_objects, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};
  auto column_str_bfr{
      ctl::make_static_array_for_overwrite<char>(total_model_descriptions_size, ALLOC_MSG(ctl::OUTPUT_COLUMN_MSG))};

  // Write the object ids and descriptions to the buffers
  row_id index{0};
  auto* str_bfs_addr{&(column_str_bfr.get()[0])};
  for (const auto& [object_id, model_description] : model_descriptions_per_object) {
    object_id_data[index] = object_id;

    // + 1 to account for the string termination char
    const auto description_size{model_description.size() + 1};

    model_description_ptrs[index] = str_bfs_addr;
    str_bfs_addr = std::copy_n(model_description.c_str(), description_size, str_bfs_addr);

    ++index;
  }

  memory::table_t descriptions_table{memory::table::create_query_scope_table(num_objects, "bpmn_model_descriptions")};
  descriptions_table->add_column<cel_int_t>(
      memory::col_name{"OBJECT_ID"}, memory::col_id{"OBJECT_ID"}, std::move(object_id_data),
      memory::create_null_flags(num_objects, operator_context), memory::column_processing_state{}, table_row_limit);
  descriptions_table->add_string_column(memory::col_name{"BPMN_MODEL_DESCRIPTION"},
                                        memory::col_id{"BPMN_MODEL_DESCRIPTION"}, std::move(model_description_ptrs),
                                        std::move(column_str_bfr),
                                        memory::create_null_flags(num_objects, operator_context), table_row_limit);

  return descriptions_table;
}

}  // namespace

bpmn_tables create_bpmn_tables_from_bpmn_graph(const bpmn_graph& graph, const memory::dictionary_t& activity_dict,
                                               const std::vector<row_id>& coverage,
                                               const memory::table_row_limit_t table_row_limit,
                                               const common::execution_context& parent_context) {
  const auto context{parent_context.create_sub_context("convert_to_tables", {})};
  return {build_bpmn_edges_table(graph, table_row_limit, context),
          build_bpmn_nodes_table(graph, table_row_limit, context),
          build_bpmn_activities(graph, activity_dict, table_row_limit, context),
          build_coverage(coverage, table_row_limit, context),
          build_bpmn_model_descriptions(graph, activity_dict, table_row_limit, context)};
}

}  // namespace celonis::accelerator::operators::process::bpmn
