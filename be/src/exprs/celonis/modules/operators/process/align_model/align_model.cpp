#include "align_model.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

#include <tbb/enumerable_thread_specific.h>

#include <cpml/conformance/alignment.h>
#include <cpml/model/transformations.h>
#include <cpml/model/bpmn_graph.h>
#include <ctl/algorithm.h>
#include <ctl/array_view.h>
#include <ctl/assert.h>
#include <ctl/conversion.h>
#include <ctl/interval.h>
#include <ctl/named_type.h>
#include <ctl/numeric.h>
#include <ctl/static_array.h>
#include <ctl/static_array_fwd.h>
#include <ctl/time.h>
#include <ctl/utility.h>
#include <ctl/utils/allocation_messages.h>
#include <ctl/utils/time_utils.h>

#include "align_model_statistics.h"
#include "align_model_types.h"
#include "modules/common/execution_context.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/row_id.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/cache/remap_variants.h"
// #include "modules/memory/table_utils.h"
#include "modules/operators/aggregation/string_aggregation.h"
#include "modules/operators/process/align_model/replay_aligned_variant.h"
#include "modules/sr_glue_code/tbb_parallel_for.h"
#include "exprs/celonis/cpml_utils/sr_context.h"
#include "legacy_embedded_ctl/static_array.h"

namespace celonis::accelerator::operators::process::align_model {

namespace {

class variant_accessor final : public cpml::variant::variant_accessor {
 public:
  explicit variant_accessor(const memory::cache::variant_entries_t& variant_entries,
                            const common::execution_context& ctx)
      : variants_{memory::cache::remap_variants<cpml::activity_id_t>(*variant_entries, std::identity{}, ctx)} {
  }

  [[nodiscard]] size_type size() const override { return ctl::cast<size_type>(variants_.size()); }

  [[nodiscard]] value_type operator[](const cpml::variant::variant_id_t id) const override {
    return variants_[id.get()];
  }

  [[nodiscard]] std::size_t count(const cpml::variant::variant_id_t id) const override {
    ctl::runtime_assert(false, "Can not return the variant count for a variant_accessor without computed counts.");
    ctl::assert_unreachable();
  }

 private:
  memory::cache::variants_t<cpml::activity_id_t> variants_;
};

constexpr std::string_view OPERATOR_NAME{"ALIGN_MODEL"};

struct prune_variants_result {
  legacy_embedded_ctl::shared_static_array<row_id> pruned_activity_ids;
  legacy_embedded_ctl::shared_static_array<row_id> pruned_trace_ids;
};

/**
 *
 * Computes a join projection vector and activity column which can be used to filter out every activity that has no
 * matching model node in the petri net.
 *
 * @param variants The variants to align
 * @param petri_net A Petri net
 * @param context The execution context we're running in
 * @return prune_variants_result containing the join projection vector and the activity col.
 */
prune_variants_result prune_variants(const memory::cache::variant_trace_cache_t& variants,
                                     const cpml::model::petri_net& petri_net,
                                     const common::execution_context& context) {
  // TODO(a.swoboda) This could be parallelized; however, this will only pay off for very large variant trace buffers
  const auto petri_net_activity_ids{ctl::transform_to<cpml::distinct_activity_ids_t>(petri_net.transitions(), &cpml::model::pn::transition::label, &cpml::model::pn::transition_id_to_transition_mapping_t::value_type::second)};
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
  prune_variants_result result{legacy_embedded_ctl::make_shared_static_array_for_overwrite<row_id>(
                                   buffer.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG)),
                               legacy_embedded_ctl::make_shared_static_array_for_overwrite<row_id>(
                                   buffer.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};
  std::ranges::copy(buffer, result.pruned_activity_ids.begin());
  std::ranges::copy(projection, result.pruned_trace_ids.begin());
  return result;
};

[[nodiscard]] std::optional<alignment_move> remap_move_type(const cpml::conformance::bpmn_move& move) {
  switch (move.move_type().value()) {
    case cpml::conformance::bpmn_move_type::SYNC: {
      debug_assert(move.optional_activity_id().has_value());
      debug_assert(move.has_valid_model_node_id());
      return alignment_move::make_sync_move(move.optional_activity_id().value(), move.model_node_id());
    }
    case cpml::conformance::bpmn_move_type::LOG: {
      debug_assert(move.optional_activity_id().has_value());
      debug_assert(move.has_valid_model_node_id());
      return alignment_move::make_log_move(move.optional_activity_id().value(), move.model_node_id());
    }
    case cpml::conformance::bpmn_move_type::MODEL_TASK: {
      debug_assert(move.optional_activity_id().has_value());
      debug_assert(move.has_valid_model_node_id());
      return alignment_move::make_model_move(move.model_node_id());
    }
    case cpml::conformance::bpmn_move_type::MODEL_GATEWAY: {
      debug_assert(!move.optional_activity_id().has_value());
      debug_assert(move.has_valid_model_node_id());
      return alignment_move::make_gateway_move(move.model_node_id());
    }
    default:
      ctl::assert_unreachable();
  }
}

[[nodiscard]] alignments_t alignment_remapping(
    const cpml::conformance::optional_bpmn_alignments_view_t cpml_bpmn_alignments,
    const common::execution_context& context) {
  auto sub_context{context.create_sub_context("alignment_remapping", {})};
  alignments_t bpmn_alignments{};
  bpmn_alignments.reserve(cpml_bpmn_alignments.size());
  for (const auto& alignment : cpml_bpmn_alignments) {
    if (alignment) {
      alignment_t bpmn_alignment{};
      const auto alignment_moves{alignment->moves()};
      bpmn_alignment.reserve(alignment_moves.size());
      for (const auto& move : alignment_moves) {
        if (auto remapped_move{remap_move_type(move)}; remapped_move) {
          bpmn_alignment.push_back(remapped_move.value());
        }
      }
      bpmn_alignments.emplace_back(bpmn_alignment);
    } else {
      bpmn_alignments.emplace_back(std::nullopt);
    }
  }
  return bpmn_alignments;
}

std::tuple<alignments_t, cpml::conformance::behavioral_relations> compute_pruned_alignments(
    const memory::cache::variant_entries_t& pruned_variants, const cpml::model::bpmn_graph& bpmn_model,
    const cpml::conformance::alignment_execution_strategy execution_strategy, const common::execution_context& context,
    format::json::json_object_t& json_alignment_stats) {
  using duration_unit_for_logging_t = std::chrono::microseconds;

  format::json::json_object_t json_compute_alignments_stats{};

  ctl::wall_timer_t compute_pruned_alignments_timer{};
  auto compute_pruned_alignments_context{context.create_sub_context("compute_pruned_alignments", {})};
  // NB assume that the variants are already "pruned", i.e., devoid of activities that are not in the Petri net
  // NB further, assume that we do not need to remap activities here
  static const std::string user_facing_name{OPERATOR_NAME};

  ctl::wall_timer_t aligner_timer{};
  const variant_accessor cpml_variant_accessor{pruned_variants, context};
  const auto function_ctx{starrocks::celonis::cpml_utils::make_sr_function_context()};
  const auto exec_settings{cpml::conformance::alignment_exec_settings::builder{}
                               .force_alignment_execution_strategy(execution_strategy)
                               .build()};

  auto [optional_bpmn_alignments, behavioral_relations,
        statistics]{cpml::conformance::align(cpml_variant_accessor, bpmn_model, function_ctx, exec_settings)};

  json_compute_alignments_stats["cpml_align_statistics"] = std::move(statistics);

  ctl::format_and_add_duration<duration_unit_for_logging_t>("time_aligner", aligner_timer.elapsed_wall_time_so_far(),
                                                            json_compute_alignments_stats);

  ctl::wall_timer_t alignment_remapping_timer{};
  auto remapped_alignments{alignment_remapping(optional_bpmn_alignments, context)};
  ctl::format_and_add_duration<duration_unit_for_logging_t>(
      "time_alignment_remapping", alignment_remapping_timer.elapsed_wall_time_so_far(), json_compute_alignments_stats);

  ctl::format_and_add_duration<duration_unit_for_logging_t>("time_compute_pruned_alignments",
                                                            compute_pruned_alignments_timer.elapsed_wall_time_so_far(),
                                                            json_alignment_stats);

  json_alignment_stats["compute_pruned_alignments_statistics"] = std::move(json_compute_alignments_stats);

  return {std::move(remapped_alignments), std::move(behavioral_relations)};
}

// todo(h.ashraf): we can replace this with a filtered-view once we have clang-16
[[nodiscard]] alignment_view_t::iterator find_end_gateway(const alignment_view_t alignment) {
  // the last gateway in the pruned_alignment is the END gateway
  const auto end_gateway_iter{
      std::ranges::find_if(std::rbegin(alignment), std::rend(alignment), &alignment_move::is_gateway_move)};

  // we cannot have a valid alignment that does not visit the END vertex
  debug_assert(end_gateway_iter != std::rend(alignment));

  // .base() for a reverse iterator points to the next element than the reverse-iterator points to
  //  This is undefined behavior if end_gateway_iter == std::rend(alignment)
  return std::prev(end_gateway_iter.base());
}

/**
 * Transforms the pruned alignment to an alignment of the full trace. In particular, we insert the missing unmapped
 * moves (log move for unmapped activities).
 *
 * @param pruned_alignment alignment of pruned variant not containing the activities that are not present in the model
 * @param full_trace unpruned trace
 * @return alignment_t for the full_trace
 */
alignment_t pruned_to_full_variant(const alignment_view_t pruned_alignment, const trace_view_t full_trace) {
  alignment_t result{};

  // The start node should lead the alignment
  const auto* pruned_start_gateway_it{std::ranges::find_if(pruned_alignment, &alignment_move::is_gateway_move)};
  debug_assert(pruned_start_gateway_it != std::end(pruned_alignment));
  result.emplace_back(*pruned_start_gateway_it);

  // for a valid alignment, the end iter always points into the span
  const auto* pruned_end_gateway_it{find_end_gateway(pruned_alignment)};

  static constexpr auto is_log_or_unmapped_move{
      [](const alignment_move& move) { return move.is_unmapped_move() || move.is_log_move(); }};

  const auto* alignment_it{std::cbegin(pruned_alignment)};
  const auto* alignment_end{std::cend(pruned_alignment)};
  const auto* trace_it{std::cbegin(full_trace)};
  const auto* trace_end{std::cend(full_trace)};

  // Ideally we want to add unmapped moved before model moves too,
  //  But this is difficult unless we do some lookahead or a more complicated logic
  while (alignment_it != alignment_end || trace_it != trace_end) {
    // Skip the start and end gateways
    if (alignment_it == pruned_start_gateway_it || alignment_it == pruned_end_gateway_it) {
      ++alignment_it;
      continue;
    }
    if (alignment_it == alignment_end) {
      result.emplace_back(alignment_move::make_unmapped_move(*trace_it));
      ++trace_it;
      continue;
    }
    if (trace_it == trace_end) {
      result.emplace_back(*alignment_it);
      ++alignment_it;
      continue;
    }

    // We add an unmapped if alignment is not model move and activities don't match
    const auto is_model_move{alignment_it->is_gateway_move() || alignment_it->is_model_move()};
    if (is_model_move) {
      result.emplace_back(*alignment_it);
      ++alignment_it;
    } else if (alignment_it->move_on_log() == *trace_it) {
      result.emplace_back(*alignment_it);
      ++trace_it;
      ++alignment_it;
    } else {
      result.emplace_back(alignment_move::make_unmapped_move(*trace_it));
      ++trace_it;
    }
  }

  // add END gateway at the end of the alignment
  result.emplace_back(*pruned_end_gateway_it);

  // sort consecutive log and model moves so that log moves come before model moves
  debug_assert(result.size() >= 2);
  for (auto begin_it{std::next(begin(result))}, end_it{std::prev(end(result))},
       sync_it{std::ranges::find_if(begin_it, end_it, &alignment_move::is_sync_move)};
       begin_it != end_it; begin_it = std::ranges::next(sync_it, 1, end_it),
                           sync_it = std::ranges::find_if(begin_it, end_it, &alignment_move::is_sync_move)) {
    std::stable_sort(begin_it, sync_it, [](const auto& lhs, const auto& rhs) {
      debug_assert(!lhs.is_sync_move());
      debug_assert(!rhs.is_sync_move());
      return is_log_or_unmapped_move(lhs) && !is_log_or_unmapped_move(rhs);
    });
  }

  // We have some assumptions about our alignments in upstream code. Verify these here once centrally
  verify_alignment_constraints(result);

  return result;
}

/**
  Accepts the pruned alignments, i.e., alignments of the pruned variants, and maps them to alignments of the full
  variants by inserting log moves for the activities not present in the model and thus were filtered out. For empty
  variants, we do not assign an alignment. The rationale behind this is that for empty variants, we obtain the
  shortest path through the model. However, we do not want these in the output alignment tables.


 * @param pruned_alignments alignments computed on the pruned variants
 * @param pruned_variants Pruned variants where activities are filtered out that are not present in the model
 * @param full_variants Unpruned variants
 * @param context The execution context we're running in
 * @return Alignments for the full variants
 */
alignments_t map_pruned_to_full_non_empty_variants(
    const ctl::array_view<const std::optional<alignment_t>> pruned_alignments,
    const memory::cache::variant_trace_cache_t& pruned_variants,
    const memory::cache::variant_trace_cache_t& full_variants, const common::execution_context& context) {
  const auto full_to_pruned_map{pruned_variants->get_case_to_trace_col_ptrs()};
  debug_assert(full_to_pruned_map.has_value());
  return memory::cast_execute_column_pointers(
      [&](auto tup) {
        const auto map_accessor{std::get<0>(tup).get_const_accessor()};
        const auto full_traces{full_variants->get_traces(context)};
        const auto full_lengths{full_variants->get_trace_lengths(context)};
        const auto num_full_traces{full_variants->get_num_traces()};
        alignments_t result(num_full_traces, std::nullopt);
        for (row_id idx{0}; idx != num_full_traces; ++idx) {
          // only include the alignment if the corresponding trace is not empty and if the pruned alignment has a value
          if (const auto& pruned_alignment{pruned_alignments.at(map_accessor[idx])};
              full_lengths.at(idx) > 0 && pruned_alignment) {
            result[idx] =
                pruned_to_full_variant(pruned_alignment.value(), std::span{full_traces[idx], full_lengths[idx]});
          }
        }
        return result;
      },
      **full_to_pruned_map);
}

}  // namespace

std::pair<alignments_t, cpml::conformance::behavioral_relations> align_model(
    const memory::cache::variant_trace_cache_t& variants, const cpml::model::bpmn_graph& bpmn_model,
    const align_model_config& config, align_model_statistics& stats, const std::string& activity_table_name,
    const common::execution_context& context) {
  ctl::wall_timer_t bpmn_graph_to_petri_net_timer{};
  const auto cpml_ctx{starrocks::celonis::cpml_utils::make_sr_function_context()};
  const auto pn{cpml::model::to_petri_net<cpml::model::compute_mappings::NO, cpml::model::filter_out_bpmn_edge_transitions::YES>(bpmn_model, cpml_ctx)};
  stats.bpmn_graph_to_petri_net = bpmn_graph_to_petri_net_timer.elapsed_wall_time_so_far();

  using duration_unit_for_logging_t = std::chrono::microseconds;
  ctl::wall_timer_t alignment_timer{};
  format::json::json_object_t json_alignment_stats{};
  const auto callback{[&json_alignment_stats](const std::string_view key, const std::size_t value) {
    json_alignment_stats[std::string{key}] = value;
  }};

  auto align_variants_context{context.create_sub_context("align_model", {})};

  // Prune variants such that we exclude activities not present in the model, as we know that these become unmapped
  // (log) moves anyway
  ctl::wall_timer_t activity_pruning_timer{};
  const auto [pruned_buffer, pruned_projection]{prune_variants(variants, pn, align_variants_context)};
  ctl::format_and_add_duration<duration_unit_for_logging_t>(
      "time_activity_pruning", activity_pruning_timer.elapsed_wall_time_so_far(), callback);
  ctl::wall_timer_t pruned_variants_computation_timer{};
  const auto pruned_variants{aggregation::compute_variant_row_ids(
      align_variants_context, config.pruned_variant_cache_key, activity_table_name, variants->get_num_traces(),
      pruned_projection, pruned_buffer, config.variant_trace_cache_manager_instance, config.grain_size)};
  ctl::format_and_add_duration<duration_unit_for_logging_t>(
      "time_pruned_variant_computation", pruned_variants_computation_timer.elapsed_wall_time_so_far(), callback);
  callback("pruned_variant_count", pruned_variants->get_num_traces());

  const auto [pruned_alignments,
              parallel_vertices]{compute_pruned_alignments(pruned_variants, bpmn_model, config.execution_strategy,
                                                           align_variants_context, json_alignment_stats)};

  ctl::wall_timer_t map_pruned_to_full_non_empty_variants_timer{};
  // Map the alignment on the pruned variants to the full variants including the filtered activities
  auto full_alignments{
      map_pruned_to_full_non_empty_variants(pruned_alignments, pruned_variants, variants, align_variants_context)};
  ctl::format_and_add_duration<duration_unit_for_logging_t>(
      "time_map_pruned_to_full_non_empty_variants",
      map_pruned_to_full_non_empty_variants_timer.elapsed_wall_time_so_far(), callback);

  ctl::format_and_add_duration<duration_unit_for_logging_t>("time_variant_alignment_total",
                                                            alignment_timer.elapsed_wall_time_so_far(), callback);
  stats.alignment_stats = std::move(json_alignment_stats);

  return {full_alignments, parallel_vertices};
}

replay_results_t replay_aligned_variants(const cpml::model::bpmn_graph& bpmn_graph, const alignments_view_t alignments,
                                         const cpml::conformance::behavioral_relations& parallel_vertices,
                                         const common::execution_context& context) {
  const auto replay_context{context.create_sub_context("replay_aligned_variants", {})};
  // bpmn_graph has an internal mutable cache. So the class is not thread safe
  tbb::enumerable_thread_specific<cpml::model::bpmn_graph> bpmn_graphs{bpmn_graph};
  auto replay_results{
      ctl::make_static_array<std::optional<replay_result_type>>(alignments.size(), ALLOC_MSG(ctl::RETURN_VALUE_MSG))};

  const auto replay_result_fn{
      [parallel_vertices = std::as_const(parallel_vertices)](
          const auto& bpmn_graph, const auto& aligned_variant) -> std::optional<replay_result_type> {
        if (aligned_variant) {
          return replay_aligned_variant(bpmn_graph, aligned_variant.value(), parallel_vertices);
        }
        return std::nullopt;
      }};

  const auto optional_error_state{sr_glue_code::non_throwing_tbb_parallel_for(tbb::blocked_range<size_t>{0, alignments.size()},
                    [&replay_results, &bpmn_graphs, &alignments = std::as_const(alignments),
                     &replay_result_fn = std::as_const(replay_result_fn)](const auto& range) {
                      const auto& bpmn_graph{bpmn_graphs.local()};
                      for (size_t index{range.begin()}; index < range.end(); ++index) {
                        const auto& aligned_variant{alignments.at(index)};
                        replay_results.at(index) = replay_result_fn(bpmn_graph, aligned_variant);
                      }
                    })};

  if (optional_error_state.has_value()) {
    throw common::internal_exception{"ALIGN_MODEL - Error within parallel replay: {} (a total of {} errors within loop).",
      optional_error_state->error_msg, optional_error_state->number_of_errors};
  }

  return replay_results;
}

void check_result_table_size_within_limit(std::string_view operator_name, std::string_view table_name,
                                          size_t table_size, row_id table_row_limit) {
  if (!memory::check_row_limit(table_size, table_row_limit)) {
    throw common::cpm_exception{
        "{}: {} result table has [{}] rows, exceeds row limit of [{}]. Please reduce the size of the "
        "event log, or use a model that produces fewer deviations.",
        operator_name, table_name, table_size, table_row_limit};
  }
}

}  // namespace celonis::accelerator::operators::process::align_model