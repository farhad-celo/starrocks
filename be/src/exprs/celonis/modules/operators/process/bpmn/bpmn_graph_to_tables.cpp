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

  auto source_id_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto target_id_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto object_id_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto object_count_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

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

  auto node_id_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto node_type_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

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
auto exec_dict_mapper(const legacy_embedded_ctl::static_array<row_id>& dict_rid_mapping) {
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

  auto node_id_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

  auto activity_name_dict_mapping{
      legacy_embedded_ctl::make_static_array_for_overwrite<row_id>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};

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
  auto object_id_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(num_objects, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto model_description_ptrs{
      legacy_embedded_ctl::make_static_array_for_overwrite<cel_string_t>(num_objects, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto column_str_bfr{
      legacy_embedded_ctl::make_static_array_for_overwrite<char>(total_model_descriptions_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

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

[[nodiscard]] consteval auto MAKE_BLOCK_TYPE_STR_BUFFER_AND_OFFSETS() {
  constexpr size_t BUFFER_SIZE{
      std::accumulate(std::cbegin(BPMN_BLOCK_TYPE_STRINGS), std::cend(BPMN_BLOCK_TYPE_STRINGS), size_t{0},
                      [](const size_t current_size, const std::string_view block_type_as_string) {
                        return current_size + block_type_as_string.size() + 1;  // +1 for \0 terminator
                      })};
  std::array<char, BUFFER_SIZE> BUFFER{};  // raw buffer containing the (null terminated) string data
  std::array<size_t, BPMN_BLOCK_TYPE_STRINGS.size()> OFFSETS{};  // offsets to the respective string data beginning
  OFFSETS.at(0) = 0;
  for (size_t idx{0}; const std::string_view BPMN_BLOCK_TYPE_AS_STRING : BPMN_BLOCK_TYPE_STRINGS) {
    char* BUFFER_OUT_PTR{std::next(BUFFER.data(), legacy_embedded_ctl::cast_signed(OFFSETS.at(idx)))};
    // copy the string to the buffer and add a null terminator at the end
    *std::ranges::copy(BPMN_BLOCK_TYPE_AS_STRING, BUFFER_OUT_PTR).out = '\0';
    if (++idx < BPMN_BLOCK_TYPE_STRINGS.size()) {
      OFFSETS.at(idx) = OFFSETS.at(idx - 1) + BPMN_BLOCK_TYPE_AS_STRING.size() + 1;
    }
  }
  return std::make_pair(BUFFER, OFFSETS);
}

constexpr auto BUFFER_AND_OFFSETS{MAKE_BLOCK_TYPE_STR_BUFFER_AND_OFFSETS()};
static_assert(BPMN_BLOCK_TYPE_STRINGS.at(0) == &BUFFER_AND_OFFSETS.first.at(BUFFER_AND_OFFSETS.second.at(0)));
static_assert(BPMN_BLOCK_TYPE_STRINGS.at(1) == &BUFFER_AND_OFFSETS.first.at(BUFFER_AND_OFFSETS.second.at(1)));
static_assert(BPMN_BLOCK_TYPE_STRINGS.at(2) == &BUFFER_AND_OFFSETS.first.at(BUFFER_AND_OFFSETS.second.at(2)));
static_assert(BPMN_BLOCK_TYPE_STRINGS.at(3) == &BUFFER_AND_OFFSETS.first.at(BUFFER_AND_OFFSETS.second.at(3)));
static_assert(BPMN_BLOCK_TYPE_STRINGS.at(4) == &BUFFER_AND_OFFSETS.first.at(BUFFER_AND_OFFSETS.second.at(4)));
static_assert(BPMN_BLOCK_TYPE_STRINGS.at(5) == &BUFFER_AND_OFFSETS.first.at(BUFFER_AND_OFFSETS.second.at(5)));

[[nodiscard]] memory::table_t build_bpmn_blocks(const bpmn_graph_with_block_structure& graph,
                                                const memory::table_row_limit_t table_row_limit,
                                                const common::execution_context& operator_context) {
  const auto& blocks{graph.blocks()};
  const auto number_of_blocks{blocks.size()};

  // column data containers
  auto block_ids_column_data{
      legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(number_of_blocks, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto parent_block_ids_column_data{
      legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(number_of_blocks, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto object_ids_column_data{
      legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(number_of_blocks, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

  static constexpr const auto& BUFFER{BUFFER_AND_OFFSETS.first};
  static constexpr const auto& OFFSETS{BUFFER_AND_OFFSETS.second};
  auto block_type_column_data_pointers{
      legacy_embedded_ctl::make_static_array_for_overwrite<cel_string_t>(number_of_blocks, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto block_type_column_data_buffer{legacy_embedded_ctl::make_static_array<char>(BUFFER, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

  // fill column data
  for (size_t idx{0}; const auto& block : blocks) {
    block_ids_column_data.at(idx) = block.block_id;
    parent_block_ids_column_data.at(idx) = block.parent_id;
    object_ids_column_data.at(idx) = block.object_id;
    const auto block_type_as_integer{to_column_value(block.block_type)};
    const auto offset{OFFSETS.at(block_type_as_integer)};
    block_type_column_data_pointers.at(idx) = &block_type_column_data_buffer.at(offset);
    ++idx;
  }

  auto bpmn_blocks_table{memory::table::create_query_scope_table(legacy_embedded_ctl::cast_signed(number_of_blocks), "bpmn_blocks")};
  bpmn_blocks_table->add_column<cel_int_t>(memory::col_name{"BLOCK_ID"}, memory::col_id{"BLOCK_ID"},
                                           std::move(block_ids_column_data),
                                           memory::create_null_flags(number_of_blocks, operator_context),
                                           memory::column_processing_state{}, table_row_limit);
  bpmn_blocks_table->add_column<cel_int_t>(memory::col_name{"PARENT_BLOCK_ID"}, memory::col_id{"PARENT_BLOCK_ID"},
                                           std::move(parent_block_ids_column_data),
                                           memory::create_null_flags(number_of_blocks, operator_context),
                                           memory::column_processing_state{}, table_row_limit);
  bpmn_blocks_table->add_column<cel_int_t>(memory::col_name{"OBJECT_ID"}, memory::col_id{"OBJECT_ID"},
                                           std::move(object_ids_column_data),
                                           memory::create_null_flags(number_of_blocks, operator_context),
                                           memory::column_processing_state{}, table_row_limit);
  bpmn_blocks_table->add_string_column(memory::col_name{"BLOCK_TYPE"}, memory::col_id{"BLOCK_TYPE"},
                                       std::move(block_type_column_data_pointers),
                                       std::move(block_type_column_data_buffer),
                                       memory::create_null_flags(number_of_blocks, operator_context), table_row_limit);

  return bpmn_blocks_table;
}

[[nodiscard]] memory::table_t build_bpmn_nodes_to_blocks(const bpmn_graph_with_block_structure& graph,
                                                         const memory::table_row_limit_t table_row_limit,
                                                         const common::execution_context& operator_context) {
  const auto& vertex_id_to_block_ids_mapping{graph.vertex_id_to_block_id_mapping()};
  const auto number_of_nodes_with_a_related_block{
      std::accumulate(vertex_id_to_block_ids_mapping.begin(), vertex_id_to_block_ids_mapping.end(), size_t{},
                      [](const size_t current_size, const auto& vertex_id_to_block_ids) {
                        const bpmn_block_ids_t& block_ids_for_vertex_id{vertex_id_to_block_ids.second};
                        return current_size + block_ids_for_vertex_id.size();
                      })};

  // column data containers
  auto vertex_ids_column_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(number_of_nodes_with_a_related_block,
                                                                              LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  auto block_ids_column_data{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(number_of_nodes_with_a_related_block,
                                                                             LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};
  // fill column data
  for (size_t idx{0}; const auto& [vertex_id, block_ids] : vertex_id_to_block_ids_mapping) {
    for (const auto& block_id : block_ids) {
      vertex_ids_column_data.at(idx) = legacy_embedded_ctl::cast<cel_int_t>(vertex_id);
      block_ids_column_data.at(idx) = block_id;
      ++idx;
    }
  }

  // Create tables and add the corresponding columns to them
  auto bpmn_nodes_to_blocks_table{memory::table::create_query_scope_table(
      legacy_embedded_ctl::cast_signed(number_of_nodes_with_a_related_block), "bpmn_nodes_to_blocks")};

  bpmn_nodes_to_blocks_table->add_column<cel_int_t>(
      memory::col_name{"NODE_ID"}, memory::col_id{"NODE_ID"}, std::move(vertex_ids_column_data),
      memory::create_null_flags(number_of_nodes_with_a_related_block, operator_context),
      memory::column_processing_state{}, table_row_limit);
  bpmn_nodes_to_blocks_table->add_column<cel_int_t>(
      memory::col_name{"BLOCK_ID"}, memory::col_id{"BLOCK_ID"}, std::move(block_ids_column_data),
      memory::create_null_flags(number_of_nodes_with_a_related_block, operator_context),
      memory::column_processing_state{}, table_row_limit);

  return bpmn_nodes_to_blocks_table;
}

}  // namespace

bpmn_tables create_bpmn_tables_from_bpmn_graph(const bpmn_graph_with_block_structure& graph,
                                               const memory::dictionary_t& activity_dict,
                                               const memory::table_row_limit_t table_row_limit,
                                               const common::execution_context& parent_context) {
  const auto context{parent_context.create_sub_context("convert_to_tables_with_block_structure", {})};
  return {build_bpmn_edges_table(graph, table_row_limit, context),
          build_bpmn_nodes_table(graph, table_row_limit, context),
          build_bpmn_activities(graph, activity_dict, table_row_limit, context),
          build_bpmn_model_descriptions(graph, activity_dict, table_row_limit, context),
          build_bpmn_blocks(graph, table_row_limit, context),
          build_bpmn_nodes_to_blocks(graph, table_row_limit, context)};
}

}  // namespace celonis::accelerator::operators::process::bpmn
