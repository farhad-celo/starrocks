#include "align_model.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_set>

#include <tbb/enumerable_thread_specific.h>

#include "align_model_statistics.h"
#ifdef CELOSTAR
#include "exprs/celonis/result_table.h"
#endif
#include "modules/common/for_each_group.h"
#ifndef CELOSTAR
#include "modules/common/hash_cache_key.h"
#endif
#include "modules/cube/align_model_table_config.h"
#ifndef CELOSTAR
#include "modules/cube/event_table_config.h"
#include "modules/cube/event_table_config_manager.h"
#include "modules/cube/table_registry/input_dependencies.h"
#endif
#include "modules/memory/column_pointers.h"
#include "modules/memory/table.h"
#include "modules/memory/table_group.h"
#include "modules/operators/aggregation/string_aggregation.h"
#include "modules/operators/framework/cached_operator_fwd.h"
#include "modules/operators/process/align_model/replay_aligned_variant.h"
#ifdef CELOSTAR
#include "modules/operators/process/alignment/alignment_statistics.h"
#else
#include "modules/operators/process/alignment/alignment_operator.h"
#endif
#include "modules/operators/process/alignment/log_aligner.h"
#include "modules/operators/process/alignment/rl_align/rl_align_configs.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/bpmn_to_pn.h"
#ifdef CELOSTAR
#include "utils/nullable_pql_value.h"

using starrocks::celonis::ResultColumn;
using starrocks::celonis::ResultTable;
#endif

namespace celonis::accelerator::operators::process::align_model {

namespace {

constexpr std::string_view OPERATOR_NAME{"ALIGN_MODEL"};

struct prune_variants_result {
  legacy_embedded_ctl::shared_static_array<row_id> pruned_activity_ids;
  legacy_embedded_ctl::shared_static_array<row_id> pruned_trace_ids;
};

prune_variants_result prune_variants(const memory::cache::variant_trace_cache_t& variants,
                                     const alignment::petri_net::petri_net_representation& petri_net,
                                     const common::execution_context& context) {
  // TODO(a.swoboda) This could be parallelized; however, this will only pay off for very large variant trace buffers
  std::unordered_set<row_id> petri_net_activity_ids{};
  std::ranges::transform(petri_net.transitions, std::inserter(petri_net_activity_ids, end(petri_net_activity_ids)),
                         std::identity{}, &decltype(petri_net.transitions)::value_type::second);
  std::vector<row_id> buffer{};
  std::vector<row_id> projection{};
  const auto traces{variants->get_traces(context)};
  const auto trace_lengths{variants->get_trace_lengths(context)};
  const auto num_traces{variants->get_num_traces()};
  for (row_id trace_idx{0}; trace_idx != num_traces; ++trace_idx) {
    for (trace_length_type i{0}; i != trace_lengths[trace_idx]; ++i) {
      const auto activity_id{traces[trace_idx][i]};
      if (petri_net_activity_ids.contains(activity_id)) {
        buffer.emplace_back(activity_id);
        projection.emplace_back(trace_idx);
      }
    }
  }
  prune_variants_result result{memory::tracking::make_shared_static_array_for_overwrite<row_id>(
                                   buffer.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context),
                               memory::tracking::make_shared_static_array_for_overwrite<row_id>(
                                   buffer.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};
  std::ranges::copy(buffer, result.pruned_activity_ids.begin());
  std::ranges::copy(projection, result.pruned_trace_ids.begin());
  return result;
};

std::optional<alignment_move> remap_move_type(const alignment::alignment_move& move,
                                              const petri_net_id_to_bpmn_mapping& mappings,
                                              const label_to_bpmn_mapping& label_to_bpmn) {
  switch (move.type()) {
    case alignment::alignment_move_type::SYNC: {
      const auto vertex_id{mappings.at(move.move_on_model().value())};
      return alignment_move{alignment_move_type::SYNC_MOVE, move.label(), vertex_id};
    }
    case alignment::alignment_move_type::LOG: {
      // We add the bpmn vertex ids to the log moves
      const auto vertex_id{label_to_bpmn.at(move.label())};
      return alignment_move{alignment_move_type::LOG_MOVE, move.label(), vertex_id};
    }
    case alignment::alignment_move_type::MODEL: {
      if (!mappings.contains(move.move_on_model().value())) {
        // If it's a model move and the move does not correspond to a bpmn vertex, return (must be an edge)
        return std::nullopt;
      }

      static constexpr auto tau_id{alignment::string_to_int_mapper::get_tau_transition_id()};
      // If labeled, then model move, else gateway move
      const auto move_type{move.label() == tau_id ? alignment_move_type::GATEWAY_MOVE
                                                  : alignment_move_type::MODEL_MOVE};
      const auto vertex_id{mappings.at(move.move_on_model().value())};
      return alignment_move{move_type, std::nullopt, vertex_id};
    }
    case alignment::alignment_move_type::UNMAPPED: {
      // Should not happen
      return alignment_move{alignment_move_type::UNMAPPED_MOVE, move.label(), std::nullopt};
    }
    default:
      legacy_embedded_ctl::assert_unreachable();
  }
}

void write_alignment_statistics(align_model_statistics& stats, const alignment::alignment_statistics& alignment_stats) {
  stats.pruned_variants_computed_optimal = alignment_stats.pruned_variants_computed_optimal;
  stats.pruned_variants_computed_relaxation_labeling = alignment_stats.pruned_variants_computed_relaxation_labeling;
  stats.optimizations_solved = alignment_stats.optimizations_solved;
  stats.successful_relaxation_labelings = alignment_stats.successful_relaxation_labelings;
  stats.alignment_cost = alignment_stats.total_cost_pruned_variants;
  stats.successfully_computed_pruned_variants = alignment_stats.successfully_computed_pruned_variants;
  stats.time_optimal = alignment_stats.time_optimal;
  stats.time_relaxation_labeling = alignment_stats.time_relaxation_labeling;
}

std::tuple<alignments_t, alignment::alignment_statistics, parallel_vertex_pairs<>> compute_pruned_alignments(
    const memory::cache::variant_trace_cache_t& pruned_variants, const bpmn_to_petri_net_result_t& result,
    const align_model_config& config, const common::execution_context& context) {
  auto compute_pruned_alignments_context{context.create_sub_context("compute_pruned_alignments", {})};
  // NB assume that the variants are already "pruned", i.e., devoid of activities that are not in the Petri net
  // NB further, assume that we do not need to remap activities here
  static const std::string user_facing_name{OPERATOR_NAME};

  // We allow to reduce transitions which do not correspond to a vertex
  std::unordered_set<std::string> keep_transitions{};
  for (const auto& [transition_str_id, _] : result.petri_net.transitions) {
    if (result.pn_str_id_to_bpmn.contains(transition_str_id)) {
      keep_transitions.emplace(transition_str_id);
    }
  }
  const std::string operator_name{OPERATOR_NAME};
  auto aligner{alignment::log_aligner_wrapper::create(result.petri_net, keep_transitions, std::nullopt,
                                                      config.log_aligner_cfg, compute_pruned_alignments_context,
                                                      operator_name)};
  auto [alignments,
        event_to_transition_mapping]{aligner(pruned_variants, compute_pruned_alignments_context, operator_name)};

  petri_net_id_to_bpmn_mapping new_mapping{};

  // The transitions in the unfolding are called events. Want to know for each event id the corresponding BPMN vertex
  for (const auto& [event_id, transition_str_id] : event_to_transition_mapping) {
    if (result.pn_str_id_to_bpmn.contains(transition_str_id)) {
      const auto vertex_id{result.pn_str_id_to_bpmn.at(transition_str_id)};
      new_mapping.try_emplace(event_id, vertex_id);
    }
  }

  parallel_vertex_pairs<> parallel_vertices{};
  const auto& behavioral_relations{aligner.precomputation_result().behavioral_relations_tf};
  for (auto it{begin(new_mapping)}; it != end(new_mapping); ++it) {
    const auto [pn_i, bpmn_i]{*it};
    for (auto jt{it}; jt != end(new_mapping); ++jt) {
      const auto [pn_j, bpmn_j]{*jt};
      if (behavioral_relations.get_relation(pn_i, pn_j) == alignment::petri_net::behavioral_relation::INTERLEAVED) {
        parallel_vertices.add(bpmn_i, bpmn_j);
      }
    }
  }

  alignments_t bpmn_alignments{};
  bpmn_alignments.reserve(alignments.size());
  for (const auto& alignment : alignments) {
    if (alignment) {
      alignment_t bpmn_alignment{};
      bpmn_alignment.reserve(alignment.value().size());
      for (const auto& move : alignment.value().data()) {
        if (auto remapped_move{remap_move_type(move, new_mapping, result.log_label_to_bpmn)}; remapped_move) {
          bpmn_alignment.push_back(remapped_move.value());
        }
      }
      bpmn_alignments.push_back(bpmn_alignment);
    } else {
      bpmn_alignments.push_back(std::nullopt);
    }
  }

  return {bpmn_alignments, aligner.statistics(), parallel_vertices};
}

constexpr bool is_gateway_move(const alignment_move& move) {
  return move.move_type == alignment_move_type::GATEWAY_MOVE;
}

// todo(h.ashraf): we can replace this with a filtered-view once we have clang-16
[[nodiscard]] std::span<const alignment_move>::iterator find_end_gateway(std::span<const alignment_move> alignment) {
  // the last gateway in the pruned_alignment is the END gateway
  const auto end_gateway_iter{std::find_if(std::rbegin(alignment), std::rend(alignment), is_gateway_move)};

  // we cannot have a valid alignment that does not visit the END vertex
  legacy_embedded_debug_assert(end_gateway_iter != std::rend(alignment));

  // .base() for a reverse iterator points to the next element than the reverse-iterator points to
  //  This is undefined behavior if end_gateway_iter == std::rend(alignment)
  return std::prev(end_gateway_iter.base());
}

alignment_t pruned_to_full_variant(std::span<const alignment_move> pruned_alignment,
                                   std::span<const trace_element_type> full_trace) {
  alignment_t result{};

  // The start node should lead the alignment
  const auto pruned_start_gateway_it{std::ranges::find_if(pruned_alignment, is_gateway_move)};
  legacy_embedded_debug_assert(pruned_start_gateway_it != std::end(pruned_alignment));
  result.emplace_back(*pruned_start_gateway_it);

  // for a valid alignment, the end iter always points into the span
  const auto pruned_end_gateway_it{find_end_gateway(pruned_alignment)};

  static constexpr auto is_log_or_unmapped_move{[](const auto& move) {
    return move.move_type == decltype(move.move_type)::UNMAPPED_MOVE ||
           move.move_type == decltype(move.move_type)::LOG_MOVE;
  }};

  auto alignment_it{std::cbegin(pruned_alignment)};
  const auto alignment_end{std::cend(pruned_alignment)};
  auto trace_it{std::cbegin(full_trace)};
  const auto trace_end{std::cend(full_trace)};

  // Ideally we want to add unmapped moved before model moves too,
  //  But this is difficult unless we do some lookahead or a more complicated logic
  while (alignment_it != alignment_end || trace_it != trace_end) {
    // Skip the start and end gateways
    if (alignment_it == pruned_start_gateway_it || alignment_it == pruned_end_gateway_it) {
      ++alignment_it;
      continue;
    }
    if (alignment_it == alignment_end) {
      result.emplace_back(alignment_move{alignment_move_type::UNMAPPED_MOVE, *trace_it, std::nullopt});
      ++trace_it;
      continue;
    }
    if (trace_it == trace_end) {
      result.emplace_back(*alignment_it);
      ++alignment_it;
      continue;
    }

    // We add an unmapped if alignment is not model move and activities don't match
    const auto is_model_move{alignment_it->move_type == alignment_move_type::GATEWAY_MOVE ||
                             alignment_it->move_type == alignment_move_type::MODEL_MOVE};
    if (is_model_move) {
      result.emplace_back(*alignment_it);
      ++alignment_it;
    } else if (alignment_it->move_on_log == *trace_it) {
      result.emplace_back(*alignment_it);
      ++trace_it;
      ++alignment_it;
    } else {
      result.emplace_back(alignment_move{alignment_move_type::UNMAPPED_MOVE, *trace_it, std::nullopt});
      ++trace_it;
    }
  }

  // add END gateway at the end of the alignment
  result.emplace_back(*pruned_end_gateway_it);

  // sort consecutive log and model moves so that log moves come before model moves
  static constexpr auto is_sync_move{[](const auto& m) { return m.move_type == alignment_move_type::SYNC_MOVE; }};

  legacy_embedded_debug_assert(result.size() >= 2);
  for (auto begin_it{std::next(begin(result))}, end_it{std::prev(end(result))},
       sync_it{std::find_if(begin_it, end_it, is_sync_move)};
       begin_it != end_it;
       begin_it = std::ranges::next(sync_it, 1, end_it), sync_it = std::find_if(begin_it, end_it, is_sync_move)) {
    std::stable_sort(begin_it, sync_it, [](const auto& lhs, const auto& rhs) {
      legacy_embedded_debug_assert(!is_sync_move(lhs));
      legacy_embedded_debug_assert(!is_sync_move(rhs));
      return is_log_or_unmapped_move(lhs) && !is_log_or_unmapped_move(rhs);
    });
  }

  return result;
}

alignments_t map_pruned_to_full_variants(std::span<const std::optional<alignment_t>> pruned_alignments,
                                         const memory::cache::variant_trace_cache_t& pruned_variants,
                                         const memory::cache::variant_trace_cache_t& full_variants,
                                         const common::execution_context& context) {
  const auto full_to_pruned_map{pruned_variants->get_case_to_trace_col_ptrs()};
  legacy_embedded_debug_assert(full_to_pruned_map.has_value());
  return memory::cast_execute_column_pointers(
      [&](auto tup) {
        const auto map_accessor{std::get<0>(tup).get_const_accessor()};
        const auto full_traces{full_variants->get_traces(context)};
        const auto full_lengths{full_variants->get_trace_lengths(context)};
        const auto num_full_traces{full_variants->get_num_traces()};
        alignments_t result(num_full_traces, std::nullopt);
        for (row_id idx{0}; idx != num_full_traces; ++idx) {
          if (const auto& pruned_alignment{pruned_alignments[map_accessor[idx]]}; pruned_alignment) {
            result[idx] =
                pruned_to_full_variant(pruned_alignment.value(), std::span{full_traces[idx], full_lengths[idx]});
          }
        }
        return result;
      },
      **full_to_pruned_map);
}

using petri_net_label_t = alignment::petri_net::petri_net_representation::label_type;
using alignment_to_string_t = std::unordered_map<petri_net_label_t, cel_string_t>;

using buffer_lookup_t = std::unordered_set<std::string_view>;
struct buffer_with_lookup {
  legacy_embedded_ctl::static_array<char> buffer;
  buffer_lookup_t buffer_lookup;
};

template <typename ENUM, auto ENUM_TO_STR>
struct enum_to_buffer_mapper {
  [[nodiscard]] cel_string_t operator()(ENUM move_type) {
    const auto str_view{ENUM_TO_STR(move_type)};
    const auto buffer_iter{buffer_lookup.find(str_view)};

    if (buffer_iter == buffer_lookup.end()) {
      throw common::internal_exception{"Could not find string {} in the buffer", str_view};
    }

    return buffer_iter->data();
  }

  const buffer_lookup_t& buffer_lookup;
};

#ifndef CELOSTAR
/**
 * Creates a string buffer for 'strings'. Also adds the NULL_STRING to string the buffer.
 * Returns the generated buffer and a set containing string views into the buffer for looking up
 * the required buffer ptrs for a given string.
 *
 * @param strings a vector containing strings - but not the NULL_STRING
 * @return buffer and lookup set
 */
[[nodiscard]] buffer_with_lookup create_buffer_for_strings(const std::vector<std::string_view>& string_views,
                                                           const common::execution_context& context) {
  const auto buffer_size{std::accumulate(
      string_views.begin(), string_views.end(), NULL_STRING.size(),
      [](const size_t accumulated, const auto& str_view) { return accumulated + str_view.size() + 1; })};

  auto buffer{
      memory::tracking::make_static_array_for_overwrite<char>(buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
  auto* buffer_ptr{buffer.begin()};

  std::unordered_set<std::string_view> buffer_lookup;

  buffer_ptr = std::ranges::copy(NULL_STRING, buffer_ptr).out;
  buffer_lookup.emplace(buffer.begin(), NULL_STRING.size());

  for (const std::string_view& str_view : string_views) {
    const auto* old_buff_ptr{buffer_ptr};
    buffer_ptr = std::ranges::copy(str_view, buffer_ptr).out;
    *buffer_ptr++ = '\0';
    buffer_lookup.emplace(old_buff_ptr, str_view.size());
  }

  legacy_embedded_debug_assert(buffer_ptr == buffer.end());
  legacy_embedded_debug_assert(!buffer_lookup.empty());

  return {std::move(buffer), std::move(buffer_lookup)};
}

[[nodiscard]] buffer_with_lookup create_buffer_for_alignment_move_type(const common::execution_context& context) {
  auto result{create_buffer_for_strings(
      {
          alignment_move_type_strings::GATEWAY,
          alignment_move_type_strings::SYNC,
          alignment_move_type_strings::MODEL,
          alignment_move_type_strings::LOG,
          alignment_move_type_strings::UNMAPPED,
      },
      context)};

  legacy_embedded_debug_assert(!result.buffer_lookup.empty());
  return {std::move(result.buffer), result.buffer_lookup};
}

[[nodiscard]] buffer_with_lookup create_buffer_for_edge_type(const common::execution_context& context) {
  return create_buffer_for_strings(
      {
          edge_type_strings::SYNC,
          edge_type_strings::MODEL,
          edge_type_strings::SKIP,
          edge_type_strings::LOG,
          edge_type_strings::UNMAPPED,
          edge_type_strings::L1_MISSING,
      },
      context);
}
#endif

/**
 * Returns ptrs into a string buffer (referenced by 'undecorated_to_decorated_buffer') for a petri_net_label_id. Since
 * these label ids could either represent activities in the trace i.e. be from the string dictionary of the activity
 * column, or represent gateways in the bpmn model, we need both the string dictionary and bpmn_to_string mapping.
 */
struct petri_net_label_id_to_string_mapper {
  [[nodiscard]] cel_string_t operator()(const alignment_move& move) const {
    if (move.move_on_log.has_value() && move.move_on_log < string_dict.get_size()) {
      const auto petri_net_label{*move.move_on_log};
      const auto iter{buffer_lookup.find(string_dict.get_string_value(petri_net_label))};
      if (iter == buffer_lookup.end()) {
        throw common::internal_exception{"Could not find activity name {} with id {} in the buffer.",
                                         string_dict.get_string_value(petri_net_label), petri_net_label};
      }
      return iter->data();
    }

    legacy_embedded_debug_assert(move.move_on_model.has_value());
    const auto bpmn_vertex_id{*move.move_on_model};
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
    buffer_entries.emplace(std::string{dict_ptr});
  }
  for (const auto& [_, vertex_label] : bpmn_to_string) {
    buffer_entries.emplace(vertex_label);
  }

  // The NULL string must be part of the string dict
  legacy_embedded_debug_assert(buffer_entries.contains(std::string(NULL_STRING.data())));

  auto buffer_size{std::accumulate(std::begin(buffer_entries), std::end(buffer_entries), size_t{0},
                                   [](const auto acc, const auto& entry) { return acc + entry.size() + 1; })};

  auto buffer{
      memory::tracking::make_static_array_for_overwrite<char>(buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

  std::unordered_set<std::string_view> buffer_lookup;

  auto* buffer_ptr{buffer.data()};
  for (const auto& entry : buffer_entries) {
    const auto* old_buffer_ptr{buffer_ptr};
    buffer_ptr = std::ranges::copy_n(entry.c_str(), legacy_embedded_ctl::cast<std::iter_difference_t<cel_string_t>>(entry.size() + 1),
                                     buffer_ptr)
                     .out;
    buffer_lookup.emplace(old_buffer_ptr, entry.size());
  }

  return {std::move(buffer), buffer_lookup};
}

struct table_sizes {
#ifdef CELOSTAR
  size_t variant_table_size;
#else
  size_t alignment_table_size;
  size_t association_table_size;
  size_t edge_class_table_size;
#endif
};

#ifndef CELOSTAR
struct align_model_table_data {
  legacy_embedded_ctl::static_array<cel_int_t> alignment_bpmn_vertex_ids;
  memory::null_flags_t alignment_bpmn_vertex_id_nulls;
  legacy_embedded_ctl::static_array<cel_string_t> alignment_activity_labels;
  legacy_embedded_ctl::static_array<char> alignment_vertex_type_buffer;
  legacy_embedded_ctl::static_array<cel_string_t> alignment_move_types;
  legacy_embedded_ctl::static_array<char> alignment_move_buffer;
  legacy_embedded_ctl::shared_static_array<row_id> alignment_to_activity_join;

  legacy_embedded_ctl::shared_static_array<row_id> association_to_alignment_join;
  legacy_embedded_ctl::shared_static_array<row_id> association_to_edge_class_join;

  legacy_embedded_ctl::static_array<row_id> edge_class_ids;
  legacy_embedded_ctl::static_array<cel_string_t> edge_class_types;
  legacy_embedded_ctl::static_array<char> edge_class_buffer;
};

[[nodiscard]] auto create_column_and_join_arrays(const size_t alignment_table_size, const size_t association_table_size,
                                                 const size_t edge_class_table_size,
                                                 const bpmn::bpmn_to_string_t& bpmn_to_string,
                                                 const memory::column_t& activity_column,
                                                 common::execution_context& context) {
  auto alignment_bpmn_vertex_ids{memory::tracking::make_static_array_for_overwrite<cel_int_t>(
      alignment_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

  memory::null_flags_t alignment_bpmn_vertex_id_nulls{memory::create_null_flags(alignment_table_size, context)};

  auto alignment_activity_labels{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
      alignment_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

  auto alignment_label_buffer_with_lookup{
      create_merged_buffer_for_alignment_labels(bpmn_to_string, *activity_column->get_string_dict(context), context)};

  auto alignment_move_types{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
      alignment_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

  auto alignment_move_buffer_with_lookup{create_buffer_for_alignment_move_type(context)};

  auto alignment_to_activity_join{memory::tracking::make_shared_static_array_for_overwrite<row_id>(
      alignment_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};

  // the association to alignment join represents the edges - the table itself is empty
  auto association_to_alignment_join{memory::tracking::make_shared_static_array_for_overwrite<row_id>(
      association_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};

  auto association_to_edge_class_join{memory::tracking::make_shared_static_array_for_overwrite<row_id>(
      association_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};

  // edge class column
  // NB: we don't need to assign edge classes since if we construct the table with this column,
  //  each row is a separate edge class - so the row-number is implicitly the edge-class-id - however, this is needed
  //  for using SOURCE/TARGET with the associations table so we produce this column
  auto edge_class_ids{memory::tracking::make_static_array_for_overwrite<row_id>(
      edge_class_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

  auto edge_class_types{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
      edge_class_table_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

  auto edge_class_buffer_with_lookup{create_buffer_for_edge_type(context)};

  return std::make_tuple(std::move(alignment_bpmn_vertex_ids), std::move(alignment_bpmn_vertex_id_nulls),
                         std::move(alignment_activity_labels), std::move(alignment_label_buffer_with_lookup),
                         std::move(alignment_move_types), std::move(alignment_move_buffer_with_lookup),
                         std::move(alignment_to_activity_join), std::move(association_to_alignment_join),
                         std::move(association_to_edge_class_join), std::move(edge_class_ids),
                         std::move(edge_class_types), std::move(edge_class_buffer_with_lookup));
}

[[nodiscard]] memory::table_group_t create_tables_and_add_columns(align_model_table_data&& table_data,
                                                                  const memory::column_t& activity_column,
                                                                  cube::registration_options options,
                                                                  cube::input_dependencies& dependencies,
                                                                  const operator_input_columns_t& input_columns,
                                                                  common::execution_context& context) {
  auto& [alignment_bpmn_vertex_ids, alignment_bpmn_vertex_id_nulls, alignment_vertex_types,
         alignment_vertex_types_buffer, alignment_move_types, alignment_move_buffer, alignment_activity_join,
         association_alignment_join, association_edge_class_join, edge_class_id_data, edge_class_types,
         edge_class_buffer]{table_data};

  const auto alignment_table_name{fmt::format("$${}$${}$$", options.cache_key, INTERNAL_ALIGNMENT_TABLE_NAME)};
  const auto association_table_name{fmt::format("$${}$${}$$", options.cache_key, INTERNAL_ASSOCIATION_TABLE_NAME)};
  const auto edge_class_table_name{fmt::format("$${}$${}$$", options.cache_key, INTERNAL_EDGE_CLASS_TABLE_NAME)};

  memory::table_t alignment_table{std::make_shared<memory::table>(
      legacy_embedded_ctl::cast<row_id>(alignment_vertex_types.size()), alignment_table_name,
      common::hash_cache_key(alignment_table_name), options.sinfo, memory::table_meta_data::make_for_operator_table(),
      memory::user_visible_table_name{alignment_table_name}, options.scope.get_table_row_limit())};
  memory::table_t association_table{std::make_shared<memory::table>(
      legacy_embedded_ctl::cast<row_id>(association_alignment_join.size()), association_table_name,
      common::hash_cache_key(association_table_name), options.sinfo, memory::table_meta_data::make_for_operator_table(),
      memory::user_visible_table_name{association_table_name}, options.scope.get_table_row_limit())};
  memory::table_t edge_class_table{std::make_shared<memory::table>(
      legacy_embedded_ctl::cast<row_id>(edge_class_id_data.size()), edge_class_table_name,
      common::hash_cache_key(edge_class_table_name), options.sinfo, memory::table_meta_data::make_for_operator_table(),
      memory::user_visible_table_name{edge_class_table_name}, options.scope.get_table_row_limit())};

  dependencies.insert(input_columns, alignment_table.get());
  dependencies.insert(input_columns, association_table.get());
  dependencies.insert(input_columns, edge_class_table.get());

  // 1. Alignment table columns and joins
  alignment_table->add_column<cel_int_t>(memory::col_name{std::string{ALIGNMENT_MODEL_VERTEX_ID}},
                                         memory::col_id{std::string{ALIGNMENT_MODEL_VERTEX_ID}},
                                         std::move(alignment_bpmn_vertex_ids), alignment_bpmn_vertex_id_nulls,
                                         memory::column_processing_state{}, options.scope.get_table_row_limit());

  const auto alignment_vertex_types_size{alignment_vertex_types.size()};
  alignment_table->add_string_column(
      memory::col_name{std::string{ALIGNMENT_ACTIVITY_LABEL}}, memory::col_id{std::string{ALIGNMENT_ACTIVITY_LABEL}},
      std::move(alignment_vertex_types), std::move(alignment_vertex_types_buffer),
      memory::create_null_flags(alignment_vertex_types_size, context), options.scope.get_table_row_limit());

  const auto alignment_move_types_size{alignment_move_types.size()};
  alignment_table->add_string_column(memory::col_name{std::string{ALIGNMENT_MOVE_TYPE}},
                                     memory::col_id{std::string{ALIGNMENT_MOVE_TYPE}}, std::move(alignment_move_types),
                                     std::move(alignment_move_buffer),
                                     memory::create_null_flags(legacy_embedded_ctl::cast<row_id>(alignment_move_types_size), context),
                                     options.scope.get_table_row_limit());

  // 2. Association column
  // CPL-9615 we need to make a copy of the data (instead of using it both here and for the join).
  // Otherwise, the usage count will never be 1, so the data can not be swapped out (also see CPL-9610).
  // Note that this is a _shared_ static array, so copy() will only perform a shallow copy!
  auto association_edge_class{legacy_embedded_ctl::make_shared_static_array<cel_int_t>(
      std::span{association_edge_class_join}, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG),
      memory::tracking::spawn_allocator<cel_int_t>(context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG)))};
  auto association_column{association_table->add_column<cel_int_t>(
      memory::col_name{std::string{ASSOCIATION_COLUMN_NAME}}, memory::col_id{std::string{ASSOCIATION_COLUMN_NAME}},
      association_edge_class, memory::create_null_flags(association_edge_class_join.size(), context),
      memory::column_processing_state{}, options.scope.get_table_row_limit())};

  // 3. Edge class table
  const auto edge_class_id_data_size{edge_class_id_data.size()};
  edge_class_table->add_column<cel_int_t>(memory::col_name{std::string{EDGE_CLASS_ID}},
                                          memory::col_id{std::string{EDGE_CLASS_ID}}, std::move(edge_class_id_data),
                                          memory::create_null_flags(edge_class_id_data_size, context),
                                          memory::column_processing_state{}, options.scope.get_table_row_limit());

  const auto edge_class_types_size{edge_class_types.size()};
  edge_class_table->add_string_column(
      memory::col_name{std::string{EDGE_CLASS_TYPE}}, memory::col_id{std::string{EDGE_CLASS_TYPE}},
      std::move(edge_class_types), std::move(edge_class_buffer),
      memory::create_null_flags(edge_class_types_size, context), options.scope.get_table_row_limit());

  // Register joins
  // 1. add ALIGNMENT -> ACTIVITY join
  options.scope.add_or_replace_join_index(alignment_table.get(), activity_column->get_owner(), alignment_activity_join,
                                          context);
  // 2. Association Table joins
  options.scope.add_or_replace_join_index(association_table.get(), alignment_table.get(), association_alignment_join,
                                          context);

  options.scope.add_or_replace_join_index(association_table.get(), edge_class_table.get(), association_edge_class_join,
                                          context);

  // Register as pseudo event table for SOURCE/TARGET
  auto& event_table_config_manager{options.scope.get_event_table_config_manager()};
  auto case_table{event_table_config_manager.get_event_table_config(activity_column->get_owner(), context)->case_table};
  // TODO(j.kruska) CPL-9041 Create align model config independent of event table config
  auto config{std::make_unique<cube::event_table_config>(false, association_table, edge_class_table, nullptr,
                                                         association_column, nullptr, nullptr, nullptr, nullptr,
                                                         nullptr, nullptr, cube::align_model_table_config{case_table})};
  event_table_config_manager.add_event_table_config(std::move(config), context);

  // Make table group
  memory::table_map_t map{};
  map.emplace(INTERNAL_ALIGNMENT_TABLE_NAME, alignment_table);
  map.emplace(INTERNAL_ASSOCIATION_TABLE_NAME, association_table);
  map.emplace(INTERNAL_EDGE_CLASS_TABLE_NAME, edge_class_table);
  return std::make_shared<memory::table_group>(std::string{TABLE_GROUP_NAME}, std::move(map));
}
#endif

struct parallel_block {
  // input range
  row_id offset_in;
  row_id size_in;
  // output ranges
#ifdef CELOSTAR
  row_id offset_variant;
#else
  row_id offset_alignment;
  row_id offset_association;
  row_id offset_edge_class;
#endif
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
              common::for_each_group(local_block.first, local_block.last, case_accessor, [&](auto interval) {
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
                legacy_embedded_debug_assert(optional_alignment_for_case.has_value() == optional_replay_result_for_case.has_value());
                if (!optional_alignment_for_case) {
                  return;
                }
#ifdef CELOSTAR
                local_block.variant_table_size++;
#else
                const auto& alignment_for_case{optional_alignment_for_case.value()};
                const auto& replay_result_for_case{optional_replay_result_for_case.value()};

                local_block.alignment_table_size += alignment_for_case.size();
                local_block.association_table_size += replay_result_for_case.num_rows();
                local_block.edge_class_table_size += replay_result_for_case.num_edge_components();
#endif
              });
            });

        std::vector<parallel_block_info> flattened_blocks{};
        for (const auto& block_vector : blocks) {
          std::ranges::copy(block_vector, std::back_inserter(flattened_blocks));
        }
        std::ranges::sort(flattened_blocks, std::ranges::less{}, &parallel_block_info::first);
        legacy_embedded_debug_assert(flattened_blocks.front().first == 0);

        std::vector<parallel_block> result(flattened_blocks.size());

        table_sizes accumulated_table_sizes{
#ifdef CELOSTAR
            .variant_table_size = 0 };
#else
            .alignment_table_size = 0, .association_table_size = 0, .edge_class_table_size = 0};
#endif
        auto output_it{begin(result)};
        for (const auto& block_info : flattened_blocks) {
          *output_it++ =
              parallel_block{.offset_in = block_info.first,
                             .size_in = block_info.last - block_info.first,
#ifdef CELOSTAR
                             .offset_variant = legacy_embedded_ctl::cast<row_id>(accumulated_table_sizes.variant_table_size)};
#else
                             .offset_alignment = legacy_embedded_ctl::cast<row_id>(accumulated_table_sizes.alignment_table_size),
                             .offset_association = legacy_embedded_ctl::cast<row_id>(accumulated_table_sizes.association_table_size),
                             .offset_edge_class = legacy_embedded_ctl::cast<row_id>(accumulated_table_sizes.edge_class_table_size)};
#endif
#ifdef CELOSTAR
          accumulated_table_sizes.variant_table_size += block_info.variant_table_size;
#else
          accumulated_table_sizes.alignment_table_size += block_info.alignment_table_size;
          accumulated_table_sizes.association_table_size += block_info.association_table_size;
          accumulated_table_sizes.edge_class_table_size += block_info.edge_class_table_size;
#endif
        }

        return std::pair{result, accumulated_table_sizes};
      },
      case_id_column, *case_to_trace_ptrs, activity_to_case_join);
}

memory::table_group_t inflate(const alignments_t& alignments, const replay_results_t& replay_results,
                              const memory::join_projection_vector_t& activity_to_case_join,
                              const memory::column_ptrs_t& case_to_trace_ptrs,
                              const bpmn::bpmn_to_string_t& bpmn_to_string, const memory::column_t& activity_column,
                              const memory::column_ptrs_abstract& case_id_column,
#ifdef CELOSTAR
                              size_t grain_size,
#else
                              const cube::registration_options& options, size_t grain_size,
                              cube::input_dependencies& dependencies, const operator_input_columns_t& input_columns,
#endif
                              common::execution_context& context) {
  // #lizard forgives
  const auto& [blocks, table_sizes]{get_blocks(alignments, replay_results, activity_to_case_join, case_to_trace_ptrs,
                                               case_id_column, grain_size, context)};
  // create arrays for column storage
#ifdef CELOSTAR
  const auto& variant_table_size = table_sizes.variant_table_size;

  auto result_table = std::make_unique<ResultTable>("align_model", variant_table_size);
  auto& alignment_model_vertex_id =
      result_table->AddColumn<std::vector<std::optional<size_t>>>("alignment_model_vertex_id");
  auto& alignment_vertex_label = result_table->AddColumn<std::vector<std::string>>("alignment_vertex_label");
  auto& alignment_move_type = result_table->AddColumn<std::vector<std::string>>("alignment_move_type");
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
      bpmn_to_string, *activity_column->get_string_dict(context),
      alignment_label_buffer_with_lookup.buffer_lookup};
#else
  const auto& [alignment_table_size, association_table_size, edge_class_table_size]{table_sizes};
  auto storages{create_column_and_join_arrays(alignment_table_size, association_table_size, edge_class_table_size,
                                              bpmn_to_string, activity_column, context)};

  // maps petri net label ids to strings to create alignment_labels - uses either the string dictionary (e.g. for
  // unmapped activities) or the bpmn model
  const petri_net_label_id_to_string_mapper petri_net_to_string_mapper{
      bpmn_to_string, *activity_column->get_string_dict(context), std::get<3>(storages).buffer_lookup};

  // maps from alignment_move_type to the alignment_move string buffer
  const enum_to_buffer_mapper<alignment_move_type, &alignment_move_to_string> alignment_move_to_buffer{
      std::get<5>(storages).buffer_lookup};

  // maps from edge_type to the edge_class string buffer
  const enum_to_buffer_mapper<edge_type, &edge_type_to_string> edge_type_to_buffer{
      std::get<11>(storages).buffer_lookup};
#endif

  // Create the data columns for the 3 tables and the corresponding join vectors:
  // Activity(1) -> (N) Alignment (1) -> (N) Association (N) -> (1) Edge Class
  tbb::parallel_for_each(
      blocks,
      // It is safe to fill blocks of the `storage` data in parallel
#ifdef CELOSTAR
      [&alignment_model_vertex_id, &alignment_vertex_label, &alignment_move_type, &alignment_activity_index,
       &association_edge_class, &association_alignment_index, &edge_class_id, &edge_class_type, &activity_column,
       &case_id_column = std::as_const(case_id_column), &case_to_trace_ptrs = std::as_const(case_to_trace_ptrs),
       &activity_to_case_join = std::as_const(activity_to_case_join), &alignments = std::as_const(alignments),
       &replay_results = std::as_const(replay_results),
       &petri_net_to_string_mapper = std::as_const(petri_net_to_string_mapper)](const parallel_block& block) {
#else
      [&storages, &case_id_column = std::as_const(case_id_column),
       &case_to_trace_ptrs = std::as_const(case_to_trace_ptrs),
       &activity_to_case_join = std::as_const(activity_to_case_join), &alignments = std::as_const(alignments),
       &replay_results = std::as_const(replay_results), &edge_type_to_buffer = std::as_const(edge_type_to_buffer),
       &petri_net_to_string_mapper = std::as_const(petri_net_to_string_mapper),
       &alignment_move_to_buffer = std::as_const(alignment_move_to_buffer)](const parallel_block& block) {
#endif
        memory::cast_execute_column_pointers(
            [&](auto tup) {
              const auto case_accessor{std::get<0>(tup).get_const_accessor()};
              if (case_accessor.size() == 0) {
                return;
              }
              const auto case_to_trace_accessor{std::get<1>(tup).get_const_accessor()};
              const auto& activity_to_case_join_vec{std::get<2>(tup)};

#ifdef CELOSTAR
              auto current_variant_row{block.offset_variant};
#else
              auto current_alignment_row{block.offset_alignment};
              auto current_association_row{block.offset_association};
              auto current_edge_class_row{block.offset_edge_class};
#endif

              common::for_each_group(
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
                    const auto& optional_replay_result_for_case{replay_results.at(variant_trace_id)};
                    legacy_embedded_debug_assert(optional_alignment_for_case.has_value() ==
                                 optional_replay_result_for_case.has_value());
                    if (!optional_alignment_for_case) {
                      return;
                    }
                    const auto& alignment_for_case{optional_alignment_for_case.value()};
                    const auto& replay_result_for_case{optional_replay_result_for_case.value()};

#ifndef CELOSTAR
                    auto& [inflated_alignment_bpmn_vertex_id, inflated_alignment_bpmn_vertex_id_nulls,
                           inflated_alignment_activity_label, alignment_activity_label_buffer_with_lookup,
                           inflated_moves, alignment_move_buffer_with_lookup, alignment_to_activity_join,
                           association_to_alignment_join, association_to_edge_class_join, edge_class_id,
                           inflated_edge_classes, edge_class_buffer_with_lookup]{storages};
#endif

                    // 1. Fill the association table
#ifdef CELOSTAR
                    association_edge_class[current_variant_row].reserve(replay_result_for_case.num_rows());
                    association_alignment_index[current_variant_row].reserve(replay_result_for_case.num_rows());
                    for (row_id id = 0; id < replay_result_for_case.components().size(); id++) {
                      for (auto vertex_id : replay_result_for_case.components()[id].edges_as_vertices) {
                        association_edge_class[current_variant_row].push_back(id);
                        association_alignment_index[current_variant_row].push_back(vertex_id);
                      }
                    }
#else
                    for (row_id current_edge_class_id{current_edge_class_row};
                         const replay_component& component : replay_result_for_case.components()) {
                      // represent edges by joining to the correct row in the alignment table, for each vertex in the
                      // component
                      std::ranges::transform(component.edges_as_vertices,
                                             std::next(begin(association_to_alignment_join), current_association_row),
                                             [current_alignment_row](size_t aligned_variant_vertex_id) {
                                               // the aligned_variant_vertex_id is just an index into the alignment
                                               // vector -> we just need to offset this by start of the current
                                               // alignment block i.e. the number of entries written in the alignment
                                               // table to get the actual row we should join to
                                               return aligned_variant_vertex_id + current_alignment_row;
                                             });
                      // fill the Association table -> Edge class table join
                      std::fill_n(std::next(std::begin(association_to_edge_class_join), current_association_row),
                                  component.size(), current_edge_class_id);
                      ++current_edge_class_id;
                      // update the output size
                      current_association_row += legacy_embedded_ctl::cast<row_id>(component.size());
                    }
#endif

                    // 2. Add the component types to the edge class table data
#ifdef CELOSTAR
                    auto edge_class_size = replay_result_for_case.components().size();
                    edge_class_id[current_variant_row].reserve(edge_class_size);
                    edge_class_type[current_variant_row].reserve(edge_class_size);
                    for (int id = 0; id < edge_class_size; id++) {
                      edge_class_id[current_variant_row].push_back(id);
                      edge_class_type[current_variant_row].emplace_back(
                          edge_type_to_string(replay_result_for_case.components()[id].component_type));
                    }
#else
                    std::ranges::transform(replay_result_for_case.components(),
                                           std::next(std::begin(inflated_edge_classes), current_edge_class_row),
                                           edge_type_to_buffer, &replay_component::component_type);
                    std::iota(std::next(std::begin(edge_class_id), current_edge_class_row),
                              std::next(std::begin(edge_class_id),
                                        current_edge_class_row + replay_result_for_case.num_edge_components()),
                              current_edge_class_row);
                    current_edge_class_row += legacy_embedded_ctl::cast<row_id>(replay_result_for_case.num_edge_components());
#endif

                    // 3. Fill ALIGNMENT table column data and join to Activity table
                    // copy the alignment (ids/move types) for each case into the arrays
#ifdef CELOSTAR
                    auto alignment_size = alignment_for_case.size();
                    alignment_model_vertex_id[current_variant_row].reserve(alignment_size);
                    alignment_vertex_label[current_variant_row].reserve(alignment_size);
                    alignment_move_type[current_variant_row].reserve(alignment_size);
                    alignment_activity_index[current_variant_row].reserve(alignment_size);
                    for (size_t offset{0}; offset != alignment_size; ++offset) {
                      const auto& move{alignment_for_case.at(offset)};
                      alignment_vertex_label[current_variant_row].emplace_back(petri_net_to_string_mapper(move));
                      alignment_move_type[current_variant_row].emplace_back(alignment_move_to_string(move.move_type));
                      if (move.move_on_model) {
                        alignment_model_vertex_id[current_variant_row].push_back(move.move_on_model.value());
                      } else {
                        alignment_model_vertex_id[current_variant_row].emplace_back();
                      }
                    }
                    for (auto index : replay_result_for_case.alignment_to_timestamp()) {
                      alignment_activity_index[current_variant_row].push_back(index);
                    }

                    current_variant_row++;
#else
                    for (size_t offset{0}; offset != alignment_for_case.size(); ++offset) {
                      const auto& move{alignment_for_case.at(offset)};
                      const auto index{current_alignment_row + offset};
                      if (move.move_on_model) {
                        inflated_alignment_bpmn_vertex_id.at(index) = move.move_on_model.value();
                      } else {
                        inflated_alignment_bpmn_vertex_id_nulls->set(index, true);
                      }
                    }

                    std::ranges::transform(alignment_for_case,
                                           std::next(begin(inflated_alignment_activity_label), current_alignment_row),
                                           petri_net_to_string_mapper);
                    std::ranges::transform(alignment_for_case, std::next(begin(inflated_moves), current_alignment_row),
                                           alignment_move_to_buffer, &alignment_move::move_type);
                    std::ranges::transform(
                        replay_result_for_case.alignment_to_timestamp(),
                        std::next(begin(alignment_to_activity_join), current_alignment_row),
                        [&interval](size_t alignment_relative_timestamp_join) {
                          return legacy_embedded_ctl::cast<row_id>(interval.begin() + alignment_relative_timestamp_join);
                        });

                    current_alignment_row += legacy_embedded_ctl::cast<row_id>(alignment_for_case.size());
#endif
                  });
            },
            case_id_column, *case_to_trace_ptrs, activity_to_case_join);
      });

#ifdef CELOSTAR
  // Make table group
  memory::table_group_t tables{};
  tables.emplace("align_model", std::move(result_table));

  return tables;
#else
  align_model_table_data table_data{std::move(std::get<0>(storages)),  std::move(std::get<1>(storages)),
                                    std::move(std::get<2>(storages)),  std::move(std::get<3>(storages).buffer),
                                    std::move(std::get<4>(storages)),  std::move(std::get<5>(storages).buffer),
                                    std::move(std::get<6>(storages)),  std::move(std::get<7>(storages)),
                                    std::move(std::get<8>(storages)),  std::move(std::get<9>(storages)),
                                    std::move(std::get<10>(storages)), std::move(std::get<11>(storages).buffer)};

  return create_tables_and_add_columns(std::move(table_data), activity_column, options, dependencies, input_columns,
                                       context);
#endif
}

}  // anonymous namespace

align_model_config align_model_config::make(std::string pruned_variant_cache_key,
                                            cube::variant_trace_cache_manager* trace_cache_manager,
                                            const std::optional<alignment::log_aligner_config>& aligner_cfg) {
  return {ALIGN_MODEL_GRAIN_SIZE, std::move(pruned_variant_cache_key), trace_cache_manager,
          aligner_cfg.value_or(alignment::log_aligner_config::make_default())};
}

std::pair<alignments_t, parallel_vertex_pairs<>> align_model(const memory::cache::variant_trace_cache_t& variants,
                                                             const bpmn_to_petri_net_result_t& result,
                                                             const align_model_config& config,
                                                             align_model_statistics& stats,
                                                             const std::string& activity_table_name,
                                                             const common::execution_context& context) {
  auto align_variants_context{context.create_sub_context("align_model", {})};
  const auto [pruned_buffer, pruned_projection]{prune_variants(variants, result.petri_net, align_variants_context)};
  const auto pruned_variants{aggregation::compute_variant_row_ids(
      align_variants_context, config.pruned_variant_cache_key, activity_table_name, variants->get_num_traces(),
      pruned_projection, pruned_buffer, *config.variant_trace_cache_manager_instance, config.grain_size)};
  const auto [pruned_alignments, alignment_statistics,
              parallel_vertices]{compute_pruned_alignments(pruned_variants, result, config, align_variants_context)};
  auto full_alignments{
      map_pruned_to_full_variants(std::span{pruned_alignments}, pruned_variants, variants, align_variants_context)};

  write_alignment_statistics(stats, alignment_statistics);
  stats.variant_count = variants->get_num_traces();
  stats.pruned_variants_count = pruned_variants->get_num_traces();

  return {full_alignments, parallel_vertices};
}

bpmn_to_petri_net_result_t bpmn_to_petri_net(const bpmn::bpmn_graph& graph) {
  // NB we assume here that there is a one-to-one mapping from petri net transitions to BPMN vertices
  bpmn_to_petri_net_result_t result{get_pn(graph), {}, {}};
  // get_pn(graph) returns a Petri net where the labeled transitions' label ids are the BPMN vertex ids.
  // However, the alignment expects the Petri net transitions' label ids to refer to the corresponding activity column
  // id if they refer to a task (that is present in the activity column), and tau label if not.
  // That means that we need to remap ids from task nodes to their activity ids and from other nodes to tau.
  // TODO (a.swoboda) fix the case where we have activities that are only in the model (and not in the activity column).
  //  NB This is not critical at the moment as we only used mined models, where such activities can't show up.
  for (auto& [transition_str_id, vertex_ref] : result.petri_net.transitions) {
    if (vertex_ref == alignment::string_to_int_mapper::get_tau_transition_id()) {
      continue;
    }

    const auto& corresponding_vertex{graph.get_vertex(vertex_ref)};
    const auto label_to_be{std::visit(
        legacy_embedded_ctl::overloaded{[](const bpmn::task& t) { return t.activity_id; },
                        [](const auto& /**/) { return alignment::string_to_int_mapper::get_tau_transition_id(); }},
        corresponding_vertex.get_vertex_type())};
    result.pn_str_id_to_bpmn.try_emplace(transition_str_id, vertex_ref);
    vertex_ref = label_to_be;
  }

  // This can also be extracted somewhere else, but we don't bother for now
  for (const auto& [vertex_id, vertex] : graph.get_vertices()) {
    if (is_task(vertex)) {
      const auto activity_id{std::get<process::bpmn::task>(vertex.get_vertex_type()).activity_id};
      if (activity_id != VALUE_NOT_FOUND) {
        if (result.log_label_to_bpmn.contains(activity_id)) {
          throw common::internal_exception(
              "Input BPMN model has duplicate tasks. "
              "This messes up with the vertex id assignment for log moves.");
        }
        result.log_label_to_bpmn.emplace(activity_id, vertex_id);
      }
    }
  }

  return result;
}

replay_results_t replay_aligned_variants(const bpmn::bpmn_graph& bpmn_graph, const alignments_t& alignments,
                                         const parallel_vertex_pairs<>& parallel_vertices,
                                         const common::execution_context& context) {
  const auto replay_context{context.create_sub_context("replay_aligned_variants", {})};
  replay_results_t replay_results{};

  std::ranges::transform(
      alignments, std::back_inserter(replay_results),
      [&bpmn_graph = std::as_const(bpmn_graph), parallel_vertices = std::as_const(parallel_vertices)](
          const auto& aligned_variant) -> std::optional<replay_result_type> {
        if (aligned_variant) {
          return replay_aligned_variant(bpmn_graph, aligned_variant.value(), parallel_vertices);
        }
        return std::nullopt;
      });

  return replay_results;
}

#ifdef CELOSTAR
memory::table_group_t create_tables(const alignments_t& alignments, const replay_results_t& replay_results,
                                    const bpmn::bpmn_to_string_t& bpmn_to_string, const variants& variants,
                                    const memory::column_t& activity_column, const memory::column_t& case_id_column,
                                    const memory::join_projection_vector_t& activity_to_case_join,
                                    const common::execution_context& context, size_t grain_size) {
  auto create_tables_context{context.create_sub_context("create_tables", {})};
  return inflate(alignments, replay_results, activity_to_case_join, variants->get_case_to_trace_col_ptrs().value(),
                 bpmn_to_string, activity_column, case_id_column->get_column_pointers(context), grain_size,
                 create_tables_context);
}
#else
memory::table_group_t create_tables(const alignments_t& alignments, const replay_results_t& replay_results,
                                    const bpmn::bpmn_to_string_t& bpmn_to_string, const variants& variants,
                                    const memory::column_t& activity_column, const memory::column_t& case_id_column,
                                    const memory::join_projection_vector_t& activity_to_case_join,
                                    const cube::registration_options& options, const common::execution_context& context,
                                    size_t grain_size, cube::input_dependencies& dependencies,
                                    const operator_input_columns_t& input_columns) {
  auto create_tables_context{context.create_sub_context("create_tables", {})};
  return inflate(alignments, replay_results, activity_to_case_join, variants->get_case_to_trace_col_ptrs().value(),
                 bpmn_to_string, activity_column, case_id_column->get_column_pointers(context), options, grain_size,
                 dependencies, input_columns, create_tables_context);
}
#endif

}  // namespace celonis::accelerator::operators::process::align_model
