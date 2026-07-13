#include "sr_specific_inflation_glue_code.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <string_view>
#include <unordered_set>

#include <tbb/enumerable_thread_specific.h>

#include <cpml/model/bpmn_graph.h>
#include <ctl/algorithm.h>
#include <ctl/interval.h>

#include "exprs/celonis/result_table.h"
#include "modules/common/case_aligned_range.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/table_group.h"
#include "modules/operators/process/align_model/deviation_category.h"
#include "modules/operators/process/align_model/replay_aligned_variant.h"
#include "modules/sr_glue_code/tbb_parallel_for.h"
#include "utils/nullable_pql_value.h"

using starrocks::celonis::ResultColumn;
using starrocks::celonis::ResultTable;

namespace celonis::accelerator::operators::process::align_model::v1 {

namespace {

using buffer_lookup_t = std::unordered_set<std::string_view>;
struct buffer_with_lookup {
  ctl::static_array<char> buffer;
  buffer_lookup_t buffer_lookup;
};

/**
 * Returns ptrs into a string buffer (referenced by 'undecorated_to_decorated_buffer') for a petri_net_label_id. Since
 * these label ids could either represent activities in the trace i.e. be from the string dictionary of the activity
 * column, or represent gateways in the bpmn model, we need both the string dictionary and bpmn_to_string mapping.
 */
struct petri_net_label_id_to_string_mapper {
  [[nodiscard]] cel_string_t operator()(const alignment_move& move) const {
    if (move.move_on_log().has_value() && move.move_on_log() < string_dict.get_size()) {
      const auto petri_net_label{*move.move_on_log()};
      const auto iter{buffer_lookup.find(string_dict.get_string_value(petri_net_label))};
      if (iter == buffer_lookup.end()) {
        throw common::internal_exception{"Could not find activity name {} with id {} in the buffer.",
                                         string_dict.get_string_value(petri_net_label), petri_net_label};
      }
      return iter->data();
    }

    debug_assert(move.move_on_model().has_value());
    const auto bpmn_vertex_id{*move.move_on_model()};
    const auto iter{buffer_lookup.find(bpmn_to_string.at(bpmn_vertex_id))};
    if (iter == buffer_lookup.end()) {
      throw common::internal_exception{"Could not find bpmn vertex with name {} and id {} in the buffer.",
                                       bpmn_to_string.at(bpmn_vertex_id), bpmn_vertex_id};
    }

    return iter->data();
  }

  const bpmn::bpmn_to_string_t& bpmn_to_string;
  const memory::string_dictionary& string_dict;
  const buffer_lookup_t& buffer_lookup;
};

[[nodiscard]] buffer_with_lookup create_merged_buffer_for_alignment_labels(const bpmn::bpmn_to_string_t& bpmn_to_string,
                                                                           const memory::string_dictionary& string_dict,
                                                                           const common::execution_context& context) {
  std::unordered_set<std::string> buffer_entries{};

  for (const auto* dict_ptr : string_dict.get_const_data(context)) {
    buffer_entries.emplace(dict_ptr);
  }
  for (const auto& [_, vertex_label] : bpmn_to_string) {
    buffer_entries.emplace(vertex_label);
  }

  // The NULL string must be part of the string dict
  debug_assert(buffer_entries.contains(std::string(NULL_STRING.data())));

  auto buffer_size{std::accumulate(std::begin(buffer_entries), std::end(buffer_entries), size_t{0},
                                   [](const auto acc, const auto& entry) { return acc + entry.size() + 1; })};

  auto buffer{
      ctl::make_static_array_for_overwrite<char>(buffer_size, ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

  std::unordered_set<std::string_view> buffer_lookup;

  auto* buffer_ptr{buffer.data()};
  for (const auto& entry : buffer_entries) {
    const auto* old_buffer_ptr{buffer_ptr};
    buffer_ptr = std::ranges::copy_n(entry.c_str(),
                                     legacy_embedded_ctl::cast<std::iter_difference_t<cel_string_t>>(entry.size() + 1),
                                     buffer_ptr)
                     .out;
    buffer_lookup.emplace(old_buffer_ptr, entry.size());
  }

  return {std::move(buffer), buffer_lookup};
}

struct table_sizes {
  size_t variant_table_size;
};

struct parallel_block {
  // input range
  row_id offset_in;
  row_id size_in;
  // output ranges
  row_id offset_variant;
};

struct parallel_block_info final : table_sizes {
  // input range
  row_id first;
  row_id last;
};

std::pair<std::vector<parallel_block>, table_sizes> get_blocks(
    const alignments_t& alignments, const replay_results_t& replay_results,
    const memory::join_projection_vector_t& activity_to_case_join, const memory::column_ptrs_t& case_to_trace_ptrs,
    const memory::column_ptrs_abstract& case_id_column, size_t grain_size, const common::execution_context& context) {
  auto get_blocks_context{context.create_sub_context("align_model::get_blocks", {})};
  return memory::cast_execute_column_pointers(
      [&](auto tup) {
        const auto case_accessor{std::get<0>(tup).get_const_accessor()};
        if (case_accessor.size() == 0) {
          return std::pair<std::vector<parallel_block>, table_sizes>{};
        }
        const auto case_to_trace_accessor{std::get<1>(tup).get_const_accessor()};
        const auto& activity_to_case_join_vec{std::get<2>(tup)};

        tbb::enumerable_thread_specific<std::vector<parallel_block_info>> blocks{};
        tbb::parallel_for(
            common::group_aligned_range{case_accessor.get(), std::next(case_accessor.get(), case_accessor.size()),
                                        grain_size, std::identity{}},
            [&blocks, &case_accessor, &activity_to_case_join_vec, &case_to_trace_accessor, &alignments,
             &replay_results](const auto& range) {
              auto& local_block{blocks.local().emplace_back()};
              local_block = {};
              local_block.first = std::distance(case_accessor.get(), range.begin);
              local_block.last = std::distance(case_accessor.get(), range.end);
              ctl::deprecated::for_each_group(local_block.first, local_block.last, case_accessor, [&](auto interval) {
                const auto activity_table_case_id_col_row{
                    interval.begin()};  // this is from the activity but the variant trace
                // cache stores col_ptrs at the case table level
                const auto case_table_row{activity_to_case_join_vec.at(activity_table_case_id_col_row)};

                // only include the case `i` in the activity table for size calculation if there is a
                // join partner between the activity and case tables for case `i`
                if (case_table_row == VALUE_NOT_FOUND) {
                  return;
                }
                const auto variant_trace_id{case_to_trace_accessor.at(case_table_row)};

                const auto& optional_alignment_for_case{alignments.at(variant_trace_id)};
                const auto& optional_replay_result_for_case{replay_results.at(variant_trace_id)};
                debug_assert(optional_alignment_for_case.has_value() == optional_replay_result_for_case.has_value());
                if (!optional_alignment_for_case) {
                  return;
                }
                local_block.variant_table_size++;
              });
            });

        std::vector<parallel_block_info> flattened_blocks{};
        for (const auto& block_vector : blocks) {
          std::ranges::copy(block_vector, std::back_inserter(flattened_blocks));
        }
        std::ranges::sort(flattened_blocks, std::ranges::less{}, &parallel_block_info::first);
        debug_assert(flattened_blocks.front().first == 0);

        std::vector<parallel_block> result(flattened_blocks.size());

        table_sizes accumulated_table_sizes{.variant_table_size = 0};
        auto output_it{begin(result)};
        for (const auto& block_info : flattened_blocks) {
          *output_it++ = parallel_block{
              .offset_in = block_info.first,
              .size_in = block_info.last - block_info.first,
              .offset_variant = legacy_embedded_ctl::cast<row_id>(accumulated_table_sizes.variant_table_size)};
          accumulated_table_sizes.variant_table_size += block_info.variant_table_size;
        }

        return std::pair{result, accumulated_table_sizes};
      },
      case_id_column, *case_to_trace_ptrs, activity_to_case_join);
}

using variant_idx_to_row_t = std::vector<row_id>;
template <typename ACTIVITY_ACCESSOR>
[[nodiscard]] variant_idx_to_row_t compute_variant_idx_to_case_idx_map(
    const ctl::half_open_interval<row_id> interval_for_case, const ACTIVITY_ACCESSOR& activity_accessor) {
  // we filter nulls from the variant, therefore to get the right event to join to we need to map the variant idx to
  // the case idx.
  variant_idx_to_row_t variant_idx_to_log_idx{};
  // the intervals are disjunct per thread an the activity_accessor is readonly, therefore this access is threadsafe
  std::ranges::copy_if(std::views::iota(interval_for_case.begin(), interval_for_case.end()),
                       std::back_inserter(variant_idx_to_log_idx), [&](const auto idx) {
                         // we skip over nulls since they do not contribute to the idx in the variant
                         return activity_accessor.at(idx) != 0;
                       });
  return variant_idx_to_log_idx;
}

memory::table_group_t inflate(const alignments_t& alignments, const replay_results_t& replay_results,
                              const deviation_categories_for_cases_view_t deviation_categories,
                              const memory::join_projection_vector_t& activity_to_case_join,
                              const memory::column_ptrs_t& case_to_trace_ptrs,
                              const bpmn::bpmn_to_string_t& bpmn_to_string, const memory::column_t& activity_column,
                              const memory::column_ptrs_abstract& case_id_column, size_t grain_size,
                              common::execution_context& context) {
  // #lizard forgives
  const auto& [blocks, table_sizes]{get_blocks(alignments, replay_results, activity_to_case_join, case_to_trace_ptrs,
                                               case_id_column, grain_size, context)};
  // create arrays for column storage
  const auto& variant_table_size = table_sizes.variant_table_size;

  auto result_table = std::make_unique<ResultTable>("align_model", variant_table_size);
  auto& alignment_model_vertex_id =
      result_table->AddColumn<std::vector<std::optional<size_t>>>("alignment_model_vertex_id");
  auto& alignment_vertex_label = result_table->AddColumn<std::vector<std::string>>("alignment_vertex_label");
  auto& alignment_move_type = result_table->AddColumn<std::vector<std::string>>("alignment_move_type");
  auto& alignment_deviation_category =
      result_table->AddColumn<std::vector<std::string>>("alignment_deviation_category");
  auto& alignment_activity_index = result_table->AddColumn<std::vector<row_id>>("alignment_activity_index");
  auto& association_edge_class = result_table->AddColumn<std::vector<row_id>>("association_edge_class");
  auto& association_alignment_index = result_table->AddColumn<std::vector<row_id>>("association_alignment_index");
  auto& edge_class_id = result_table->AddColumn<std::vector<row_id>>("edge_class_id");
  auto& edge_class_type = result_table->AddColumn<std::vector<std::string>>("edge_class_type");

  // maps petri net label ids to strings to create alignment_labels - uses either the string dictionary (e.g. for
  // unmapped activities) or the bpmn model
  auto alignment_label_buffer_with_lookup{
      create_merged_buffer_for_alignment_labels(bpmn_to_string, *activity_column->get_string_dict(context), context)};
  const petri_net_label_id_to_string_mapper petri_net_to_string_mapper{
      bpmn_to_string, *activity_column->get_string_dict(context), alignment_label_buffer_with_lookup.buffer_lookup};

  // Create the data columns for the 3 tables and the corresponding join vectors:
  // Activity(1) -> (N) Alignment (1) -> (N) Association (N) -> (1) Edge Class
  sr_glue_code::non_throwing_tbb_parallel_for_each(
      blocks,
      // It is safe to fill blocks of the `storage` data in parallel
      [&alignment_model_vertex_id, &alignment_vertex_label, &alignment_move_type, &alignment_deviation_category,
       &alignment_activity_index, &association_edge_class, &association_alignment_index, &edge_class_id,
       &edge_class_type, &activity_column, &case_id_column = std::as_const(case_id_column),
       &case_to_trace_ptrs = std::as_const(case_to_trace_ptrs),
       &activity_to_case_join = std::as_const(activity_to_case_join), &alignments = std::as_const(alignments),
       &replay_results = std::as_const(replay_results),
       &petri_net_to_string_mapper = std::as_const(petri_net_to_string_mapper), &context,
       &deviation_categories](const parallel_block& block) {
        memory::cast_execute_column_pointers(
            [&](auto tup) {
              const auto activity_accessor{std::get<0>(tup).get_const_accessor()};
              const auto case_accessor{std::get<1>(tup).get_const_accessor()};
              if (case_accessor.size() == 0) {
                return;
              }
              const auto case_to_trace_accessor{std::get<2>(tup).get_const_accessor()};
              const auto& activity_to_case_join_vec{std::get<3>(tup)};

              auto current_variant_row{block.offset_variant};

              ctl::deprecated::for_each_group(
                  block.offset_in, block.offset_in + block.size_in, case_accessor, [&](auto interval) {
                    const auto activity_table_case_id_col_row{interval.begin()};
                    // the above is from the activity table but the variant trace
                    // cache stores col_ptrs at the case table level
                    const auto case_table_row{activity_to_case_join_vec.at(activity_table_case_id_col_row)};

                    // if there is no join between the activity and case tables for this interval then we don't do
                    // anything
                    if (case_table_row == VALUE_NOT_FOUND) {
                      return;
                    }
                    const auto variant_trace_id{case_to_trace_accessor.at(case_table_row)};
                    const auto& optional_alignment_for_case{alignments.at(variant_trace_id)};
                    const auto& deviation_categories_for_case{deviation_categories.at(variant_trace_id)};
                    const auto& optional_replay_result_for_case{replay_results.at(variant_trace_id)};
                    debug_assert(optional_alignment_for_case.has_value() ==
                                 optional_replay_result_for_case.has_value());
                    if (!optional_alignment_for_case) {
                      return;
                    }
                    const auto& alignment_for_case{optional_alignment_for_case.value()};
                    const auto& replay_result_for_case{optional_replay_result_for_case.value()};

                    // 1. Fill the association table
                    association_edge_class[current_variant_row].reserve(replay_result_for_case.num_rows());
                    association_alignment_index[current_variant_row].reserve(replay_result_for_case.num_rows());
                    for (row_id id = 0; id < replay_result_for_case.components().size(); id++) {
                      for (auto vertex_id : replay_result_for_case.components()[id].edges_as_vertices) {
                        association_edge_class[current_variant_row].push_back(id);
                        association_alignment_index[current_variant_row].push_back(vertex_id);
                      }
                    }

                    // 2. Add the component types to the edge class table data
                    auto edge_class_size = replay_result_for_case.components().size();
                    edge_class_id[current_variant_row].reserve(edge_class_size);
                    edge_class_type[current_variant_row].reserve(edge_class_size);
                    for (int id = 0; id < edge_class_size; id++) {
                      edge_class_id[current_variant_row].push_back(id);
                      edge_class_type[current_variant_row].emplace_back(
                          edge_type_to_string(replay_result_for_case.components()[id].component_type));
                    }

                    // 3. Fill ALIGNMENT table column data and join to Activity table
                    // copy the alignment (ids/move types) for each case into the arrays
                    auto alignment_size = alignment_for_case.size();
                    alignment_model_vertex_id[current_variant_row].reserve(alignment_size);
                    alignment_vertex_label[current_variant_row].reserve(alignment_size);
                    alignment_move_type[current_variant_row].reserve(alignment_size);
                    alignment_activity_index[current_variant_row].reserve(alignment_size);
                    for (size_t offset{0}; offset != alignment_size; ++offset) {
                      const auto& move{alignment_for_case.at(offset)};
                      alignment_vertex_label[current_variant_row].emplace_back(petri_net_to_string_mapper(move));
                      alignment_move_type[current_variant_row].emplace_back(alignment_move_to_string(move.move_type()));
                      alignment_deviation_category[current_variant_row].emplace_back(
                          deviation_category_to_string(deviation_categories_for_case.at(offset)));
                      if (move.move_on_model()) {
                        alignment_model_vertex_id[current_variant_row].push_back(move.move_on_model().value());
                      } else {
                        alignment_model_vertex_id[current_variant_row].emplace_back();
                      }
                    }

                    auto variant_idx_to_row_map{compute_variant_idx_to_case_idx_map(interval, activity_accessor)};
                    for (auto alignment_idx : replay_result_for_case.alignment_to_preceding_move()) {
                      auto variant_idx{replay_result_for_case.alignment_idx_to_log_idx().at(alignment_idx)};
                      // variant_idx is an index into the variant, which points to an activity
                      // Then we map the index into the variant to an index into the case (these indices may
                      // differ if the case has null activities, e.g. B in case <A,null,B> would have variant
                      // index 1 but case index 2)
                      const auto row_idx{variant_idx_to_row_map.at(variant_idx)};
                      // row_idx indexes into the entire column, however the join in our result array is relative to the
                      // start of the case so we need to subtract by the start of the case
                      alignment_activity_index[current_variant_row].push_back(row_idx - interval.begin());
                    }

                    current_variant_row++;
                  });
            },
            activity_column->get_column_pointers(context), case_id_column, *case_to_trace_ptrs, activity_to_case_join);
      })
      .log_and_rethrow_if_has_error("ALIGN_MODEL - Error within parallel inflation");

  // Make table group
  memory::table_group_t tables{};
  tables.emplace("align_model", std::move(result_table));

  return tables;
}

}  // anonymous namespace

memory::table_group_t create_tables(const alignments_t& alignments, const replay_results_t& replay_results,
                                    const deviation_categories_for_cases_view_t deviation_categories,
                                    const bpmn::bpmn_to_string_t& bpmn_to_string, const variants& variants,
                                    const memory::column_t& activity_column, const memory::column_t& case_id_column,
                                    const memory::join_projection_vector_t& activity_to_case_join,
                                    const common::execution_context& context, size_t grain_size) {
  auto create_tables_context{context.create_sub_context("create_tables", {})};
  return inflate(alignments, replay_results, deviation_categories, activity_to_case_join,
                 variants->get_case_to_trace_col_ptrs().value(), bpmn_to_string, activity_column,
                 case_id_column->get_column_pointers(context), grain_size, create_tables_context);
}

}  // namespace celonis::accelerator::operators::process::align_model::v1