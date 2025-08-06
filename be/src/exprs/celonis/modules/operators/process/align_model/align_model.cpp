#include "align_model.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_set>

#include <tbb/enumerable_thread_specific.h>
#include <cpml/model/bpmn_graph.h>

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
                                              const bpmn::petri_net_id_to_bpmn_mapping& mappings,
                                              const bpmn::label_to_bpmn_mapping& label_to_bpmn) {
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
    const memory::cache::variant_trace_cache_t& pruned_variants, const bpmn::bpmn_to_petri_net_result_t& result,
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

  bpmn::petri_net_id_to_bpmn_mapping new_mapping{};

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

}  // anonymous namespace

align_model_config align_model_config::make(std::string pruned_variant_cache_key,
                                            cube::variant_trace_cache_manager* trace_cache_manager,
                                            const std::optional<alignment::log_aligner_config>& aligner_cfg) {
  return {ALIGN_MODEL_GRAIN_SIZE, std::move(pruned_variant_cache_key), trace_cache_manager,
          aligner_cfg.value_or(alignment::log_aligner_config::make_default())};
}

std::pair<alignments_t, parallel_vertex_pairs<>> align_model(const memory::cache::variant_trace_cache_t& variants,
                                                             const bpmn::bpmn_to_petri_net_result_t& result,
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

replay_results_t replay_aligned_variants(const cpml::model::bpmn_graph& bpmn_graph, const alignments_t& alignments,
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

}  // namespace celonis::accelerator::operators::process::align_model
