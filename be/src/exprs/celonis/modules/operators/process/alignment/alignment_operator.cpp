#include "alignment_operator.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/memory/batched_tracking_memory_resource.h"
#include "legacy_embedded_ctl/static_array.h"
#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/common/hash_cache_key.h"
#include "modules/common/shared_types.h"
#include "modules/common/timer.h"
#include "modules/cube/event_table_config.h"
#include "modules/cube/event_table_config_manager.h"
#include "modules/cube/execution/operator_executor.h"
#include "modules/cube/execution/tracking/operator_tracker.h"
#include "modules/cube/query_scope.h"
#include "modules/cube/variant_trace_cache_manager.h"
#include "modules/memory/builders/cache_column_from_data.h"
#include "modules/memory/builders/temp_column_builder.h"
#include "modules/memory/column.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/merge_dictionaries.h"
#include "modules/memory/table.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"
#include "modules/operators/process/alignment/alignment_statistics.h"
#include "modules/operators/process/alignment/log_aligner.h"
#include "modules/operators/process/alignment/log_alignment_result.h"
#include "modules/operators/process/alignment/log_alignment_result_cache.h"
#include "modules/operators/process/petri_net/petri_net_conversion.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

namespace {

std::string get_user_facing_name(const RLAlignOperatorNode& node) {
  if (node.type() == RLAlignOperatorNode::ACTIVITY) {
    return "ALIGN_ACTIVITY";
  }
  return "ALIGN_MOVE";
}

alignment_input_columns execute_input_columns(cube::execution::tracking::operator_tracker& tracker,
                                              cube::operator_executor& executor,
                                              common::execution_context& operator_context,
                                              const RLAlignOperatorNode& node,
                                              framework::operator_node* activity_column_operator_node,
                                              framework::operator_node* pruned_activity_column_operator_node,
                                              framework::operator_node* variant_column_operator_node,
                                              framework::operator_node* pruned_variant_column_operator_node) {
  auto activity_column_result{activity_column_operator_node->execute(tracker, executor, operator_context)};
  auto pruned_activity_column_result{
      pruned_activity_column_operator_node->execute(tracker, executor, operator_context)};

  // Have to verify here because variant column does not allow filtered input (and therefore our operator also doesn't)
  if (activity_column_result->get_processing_state().is_filter_unstable()) {
    throw common::cpm_exception{
        "{}: Filtered input is not supported. To map inputs to null consider using a CASE "
        "WHEN or REMAP_VALUES.",
        get_user_facing_name(node)};
  }
  auto variant_column_result{variant_column_operator_node->execute(tracker, executor, operator_context)};
  auto pruned_variant_column_result{pruned_variant_column_operator_node->execute(tracker, executor, operator_context)};
  return alignment_input_columns{.activity_column = activity_column_result,
                                 .pruned_activity_column = pruned_activity_column_result,
                                 .variant_column = variant_column_result,
                                 .pruned_variant_column = pruned_variant_column_result};
}

struct exec_compute_join_vector {
  row_id variant_row_count;
  row_id activity_table_size;
  const vector_of_alignments& alignments;
  const std::string& operator_name;
  const common::execution_context& context;

  exec_compute_join_vector(row_id variant_row_count, row_id activity_table_size, const vector_of_alignments& alignments,
                           const std::string& operator_name, const common::execution_context& context)
      : variant_row_count{variant_row_count},
        activity_table_size{activity_table_size},
        alignments{alignments},
        operator_name{operator_name},
        context{context} {}

  template <class TUPLE>
  std::pair<memory::join_raw_t, row_id> operator()(const TUPLE& t) {
    auto activity_column_ptrs_ac{std::get<0>(t).get_const_accessor()};
    auto variant_ids_ac{std::get<1>(t).get_const_accessor()};
    const auto& projection_vector{std::get<2>(t)};

    // Create output column
    const auto alignment_table_size{
        compute_alignment_table_size(activity_column_ptrs_ac, variant_ids_ac, projection_vector)};
    auto join_column{
        memory::create_raw_join(alignment_table_size, activity_table_size, memory::zero_init_t{false}, context)};

    row_id previous_projected_case{-1};
    size_t join_column_index{0};

    memory::cast_execute_join(
        [&](auto& join) {
          using join_type_t = std::remove_const_t<typename std::decay_t<decltype(join)>::value_type>;

          for (row_id i{0}; i < activity_table_size; ++i) {
            if (activity_column_ptrs_ac[i] == 0) {
              continue;
            }
            const auto current_projected_case{projection_vector[i]};
            if (current_projected_case == VALUE_NOT_FOUND) {
              continue;
            }

            // TODO (goulart.e) the sequence of offsets w.r.t. the activity joins can be calculated per variant
            if (current_projected_case != previous_projected_case) {
              // New case starts. Get the corresponding alignment
              previous_projected_case = current_projected_case;
              auto current_variant_id{variant_ids_ac[current_projected_case]};
              // If the current alignment is empty, we skip the output for this case
              if (!alignments.at(current_variant_id).has_value()) {
                continue;
              }

              bool has_passed_first_move_on_log{false};
              for (const auto& move : alignments.at(current_variant_id).value().data()) {
                // After the first move on log, we increment i for each move on log that we see
                if (has_passed_first_move_on_log && !move.is_model()) {
                  do {
                    ++i;
                  } while (activity_column_ptrs_ac[i] == 0);
                }
                has_passed_first_move_on_log = has_passed_first_move_on_log || move.is_move_on_log();

                const auto event_id{static_cast<join_type_t>(i)};
                join[static_cast<std::ptrdiff_t>(join_column_index)] = event_id;
                ++join_column_index;
              }
            }
          }
        },
        join_column);

    return {std::move(join_column), alignment_table_size};
  }

  template <class ACTIVITY_CONST_ACCESSOR, class VARIANT_CONST_ACCESSOR, class PROJECTION_VECTOR>
  [[nodiscard]] row_id compute_alignment_table_size(const ACTIVITY_CONST_ACCESSOR& activity_column_ptrs_ac,
                                                    const VARIANT_CONST_ACCESSOR& variant_ids_ac,
                                                    const PROJECTION_VECTOR& projection_vector) {
    size_t alignment_table_count{0};
    row_id current_case{-1};

    // We need to exclude cases that have all activities NULL
    for (row_id i{0}; i < activity_table_size; ++i) {
      if (activity_column_ptrs_ac[i] == 0 || projection_vector[i] == current_case ||
          projection_vector[i] == VALUE_NOT_FOUND) {
        continue;
      }

      current_case = projection_vector[i];
      const auto variant_id{variant_ids_ac[current_case]};
      if (variant_id == 0 || !alignments.at(variant_id).has_value()) {
        continue;
      }
      alignment_table_count += alignments.at(variant_id).value().size();
    }

    if (alignment_table_count > static_cast<size_t>(ROW_ID_MAX)) {
      throw common::cpm_exception{
          "{}: The resulting alignment table is too large. The maximum number of rows is {}, but got [{}].",
          operator_name, ROW_ID_MAX, alignment_table_count};
    }

    return static_cast<row_id>(alignment_table_count);
  }
};

/**
 * Generates a vector with size = number of distinct variants in the full variant column.
 *  Where each entry corresponds to the index of the pruned version of the full variant
 */
class exec_fill_variants_to_pruned_variants_map {
 public:
  explicit exec_fill_variants_to_pruned_variants_map(row_id distinct_variants, const common::execution_context& context)
      : distinct_variants_{distinct_variants}, context{context} {}

  template <class TUPLE>
  legacy_embedded_ctl::static_array<row_id> operator()(const TUPLE& t) const {
    auto mapping_result{memory::tracking::make_static_array_value_init<row_id>(
        distinct_variants_, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

    const auto variant_column_ptrs_ac{std::get<0>(t).get_const_accessor()};
    const auto pruned_variant_column_ptrs_ac{std::get<1>(t).get_const_accessor()};

    const auto variant_column_ptrs_size{variant_column_ptrs_ac.size()};
    legacy_embedded_debug_assert(variant_column_ptrs_size == pruned_variant_column_ptrs_ac.size());

    for (size_t i{0}; i < variant_column_ptrs_size; ++i) {
      const auto variant_id{static_cast<row_id>(variant_column_ptrs_ac[i])};
      const auto pruned_variant_id{static_cast<row_id>(pruned_variant_column_ptrs_ac[i])};
      legacy_embedded_debug_assert(variant_id < distinct_variants_);
      mapping_result[variant_id] = pruned_variant_id;
    }

    return mapping_result;
  }

 private:
  row_id distinct_variants_;
  const common::execution_context& context;
};

class pruned_to_full_variant_mapper {
 public:
  explicit pruned_to_full_variant_mapper(legacy_embedded_ctl::static_array<row_id> variant_to_pruned_variant_map)
      : variant_to_pruned_variant_map_{std::move(variant_to_pruned_variant_map)} {}

  vector_of_alignments execute(const vector_of_alignments& pruned_alignments, const common::execution_context& context,
                               const memory::cache::variant_trace_cache_t& variant_trace_cache) {
    const auto traces{variant_trace_cache->get_traces(context)};
    const auto trace_lengths{variant_trace_cache->get_trace_lengths(context)};

    // Generate alignments for each pruned_alignment
    const auto number_of_variants{variant_trace_cache->get_num_traces()};
    vector_of_alignments alignments(pruned_alignments.get_allocator());
    alignments.resize(number_of_variants);

    tbb::parallel_for(tbb::blocked_range<row_id>{0, number_of_variants}, [&](const auto range) {
      for (row_id i{range.begin()}; i < range.end(); ++i) {
        const auto* trace{traces[i]};
        const auto trace_length{trace_lengths[i]};

        const auto pruned_variant_id{variant_to_pruned_variant_map_[i]};
        const auto& pruned_alignment{pruned_alignments[pruned_variant_id]};

        if (pruned_alignment) {
          alignments.at(i) = fill_unmapped_log_moves(pruned_alignment.value(), trace, trace_length);
        }
      }
    });

    return alignments;
  }

 private:
  [[nodiscard]] static trace_alignment fill_unmapped_log_moves(const trace_alignment& pruned_alignment,
                                                               const int16_t* trace_ptr, size_t trace_length) {
    trace_alignment result{};
    result.reserve(pruned_alignment.visible_model_moves_count() + trace_length);

    // Insert "unmapped" log moves with single pass over pruned alignment and original trace
    auto alignment_it{std::cbegin(pruned_alignment.data())};
    const auto alignment_end{std::cend(pruned_alignment.data())};
    const std::span<const trace_element_type> trace{trace_ptr, trace_length};
    auto trace_it{std::cbegin(trace)};
    const auto trace_end{std::cend(trace)};

    // Ideally we want to add unmapped moved before model moves too,
    //  But this is difficult unless we do some lookahead or a more complicated logic
    while (alignment_it != alignment_end || trace_it != trace_end) {
      if (alignment_it == alignment_end) {
        result.add(alignment_move::unmapped(*trace_it));
        ++trace_it;
        continue;
      }
      if (trace_it == trace_end) {
        if (!alignment_it->is_tau()) {
          result.add(*alignment_it);
        }
        ++alignment_it;
        continue;
      }

      // We add an unmapped move if alignment is not model move and activities don't match
      if (alignment_it->is_model()) {
        // We don't add if it's a tau move
        if (!alignment_it->is_tau()) {
          result.add(*alignment_it);
        }
        ++alignment_it;
      } else if (alignment_it->label() == *trace_it) {
        result.add(*alignment_it);
        ++alignment_it;
        ++trace_it;
      } else {
        result.add(alignment_move::unmapped(*trace_it));
        ++trace_it;
      }
    }

    return result;
  }

  legacy_embedded_ctl::static_array<row_id> variant_to_pruned_variant_map_;
};

/**
 * The align_table is stored in cached_table_registry with the table_cache_key,
 * which is derived from the table configuration key.
 */
std::pair<std::string, std::string> build_table_cache_and_configuration_key(const memory::table_t& event_table,
                                                                            const RLAlignOperatorNode& node,
                                                                            const std::string& operator_name) {
  std::string petri_net_cache_key{node.petri_net_cache_key()};
  std::string table_configuration_key{fmt::format("{},{}", event_table->get_name(), petri_net_cache_key)};
  std::string table_cache_key{fmt::format("$${}_TABLE_{}$$", operator_name, table_configuration_key)};
  return {table_cache_key, table_configuration_key};
}

}  // namespace

alignment_operator::alignment_operator(memory::column_t activity_column, memory::column_t pruned_activity_column,
                                       memory::column_t variant_column, memory::column_t pruned_variant_column,
                                       cube::table_data& common_table_data,
                                       memory::join_projection_vector_t projection_vector,
                                       const cube::event_table_config* event_config,
                                       operators::process::alignment::log_alignment_result_cache& log_alignment_cache,
                                       const RLAlignOperatorNode& node, cube::query_scope& scope,
                                       common::execution_context& operator_context, log_aligner_config log_aligner_cfg,
                                       cube::execution::tracking::add_telemetry_counter_fn add_telemetry_counter)
    : activity_column_{std::move(activity_column)},
      pruned_activity_column_{std::move(pruned_activity_column)},
      variant_column_{std::move(variant_column)},
      variant_cache_{
          scope.get_variant_trace_cache_manager().retrieve_variant_cache(this->variant_column_->get_cache_key())},
      pruned_variant_column_{std::move(pruned_variant_column)},
      pruned_variant_cache_{scope.get_variant_trace_cache_manager().retrieve_variant_cache(
          this->pruned_variant_column_->get_cache_key())},
      common_table_data_{common_table_data},
      projection_vector_{std::move(projection_vector)},
      table_registry_{scope.get_cached_table_registry()},
      log_alignment_cache_{log_alignment_cache},
      event_config_{event_config},
      node_{node},
      log_aligner_cfg_{std::move(log_aligner_cfg)},
      scope_{scope},
      column_cache_key_{get_user_facing_name(node_)},
      add_telemetry_counter_{std::move(add_telemetry_counter)} {
  if (!this->variant_column_) {
    throw common::internal_exception{"ALIGN: Invalid variant column."};
  }
  if (!variant_cache_) {
    throw common::internal_exception{
        "ALIGN: VARIANT is expected and currently missing. Please make sure to call this function with the "
        "VARIANT function as argument."};
  }

  auto [table_cache_key, _]{build_table_cache_and_configuration_key(event_config->event_table, node_, OPERATOR_KEY)};
  std::scoped_lock alignment_table_data_log(common_table_data.table_mutex);

  if (!common_table_data.table->is_row_count_set()) {
    const auto log_alignment{calculate_log_alignment(table_cache_key, operator_context)};
    auto [output_table_join_vector, output_table_size] = memory::cast_execute_column_pointers(
        exec_compute_join_vector(variant_column_->get_row_count(operator_context), activity_column_->get_row_count(),
                                 log_alignment->get_alignments(), get_user_facing_name(node_), operator_context),
        activity_column_->get_column_pointers(operator_context), variant_column_->get_column_pointers(operator_context),
        projection_vector_);
    common_table_data.table->verify_set_row_count(output_table_size, scope_.get_table_row_limit());
    table_registry_.register_join_vector(common_table_data.table, event_config_->event_table.get(),
                                         output_table_join_vector, cube::filter_propagation_mode::DISCARD,
                                         operator_context);
  }
  table_cache_key_ = table_cache_key;
}

[[nodiscard]] const std::string& alignment_operator::get_operator_tracking_key() const noexcept { return OPERATOR_KEY; }

memory::table* alignment_operator::get_common_table() { return common_table_data_.table.get(); }

operator_input_columns_t alignment_operator::get_input_columns() const {
  return {activity_column_, variant_column_, pruned_activity_column_, pruned_variant_column_};
}

memory::column_processing_state alignment_operator::get_result_state() { return memory::column_processing_state(); }

const std::string& alignment_operator::get_cache_key() { return column_cache_key_; }

log_alignment_result_t alignment_operator::calculate_log_alignment(const std::string& alignment_table_cache_key,
                                                                   common::execution_context& operator_context) {
  return log_alignment_cache_.get_cached_or_compute_log_alignment(alignment_table_cache_key, [&]() {
    const auto user_facing_name{get_user_facing_name(node_)};

    // Validate Petri Net representation
    petri_net::check_is_valid_petri_net(node_.petri_net_description(), user_facing_name);
    petri_net::check_is_workflow_net(node_.petri_net_description(), user_facing_name);

    if (const auto duplicates_warning{petri_net::get_duplicated_activities_string(node_.petri_net_description())};
        duplicates_warning.has_value()) {
      constexpr std::string_view warning_msg{
          "{}: Your model contains duplicate activities: {}. "
          "Repeated activity names can degrade the quality of the computed alignments."};
      warnings->emplace(fmt::format(warning_msg, user_facing_name, duplicates_warning.value()));
      log::jwarn(fmt::format("{}: Model contains duplicate activities. Repeated activity names can degrade the quality "
                             "of the computed alignments.",
                             user_facing_name),
                 {{"duplicate_activities", duplicates_warning.value()}});
    }

    // Dictify activity column and get dictionary
    const auto dict{activity_column_->get_string_dict(operator_context)};
    const auto pruned_dict{pruned_activity_column_->get_string_dict(operator_context)};
    std::vector<std::pair<memory::dictionary_t, std::string>> dict_vector{
        {dict, activity_column_->get_name()}, {pruned_dict, pruned_activity_column_->get_name()}};
    auto merged_dictionaries{
        memory::merge_n_dictionaries_raw(dict_vector, get_user_visible_operator_name(), operator_context)};
    const auto pruned_dict_mapping{std::move(merged_dictionaries.mappings.at(1))};
    legacy_embedded_debug_assert(static_cast<row_id>(pruned_dict_mapping.size()) == pruned_dict->get_size());

    string_to_int_mapper str_mapper{dict};
    const auto pn_repr{petri_net::get_pn_repr_from_operator_input(node_.petri_net_description(), str_mapper)};

    std::unordered_set<std::string> keep_transitions{};
    for (const auto& [transition_str_id, label] : pn_repr.transitions) {
      if (label != string_to_int_mapper::get_tau_transition_id()) {
        keep_transitions.emplace(transition_str_id);
      }
    }
    auto aligner{log_aligner_wrapper::create(pn_repr, keep_transitions, pruned_dict_mapping, log_aligner_cfg_,
                                             operator_context, user_facing_name)};
    auto [pruned_alignments, _]{aligner(pruned_variant_cache_, operator_context, user_facing_name)};
    const auto distinct_variants_count{variant_cache_->get_num_traces()};
    auto stats{aligner.statistics()};
    stats.variant_count = distinct_variants_count;

    auto variant_to_pruned_variant_map{memory::cast_execute_column_pointers(
        exec_fill_variants_to_pruned_variants_map{distinct_variants_count, operator_context},
        variant_column_->get_column_pointers(operator_context),
        pruned_variant_column_->get_column_pointers(operator_context))};
    pruned_to_full_variant_mapper pruned_to_full_variant_mapper_obj{std::move(variant_to_pruned_variant_map)};
    auto alignments{pruned_to_full_variant_mapper_obj.execute(pruned_alignments, operator_context, variant_cache_)};

    stats.log_to_operator_statistics(add_telemetry_counter_);
    return std::make_shared<log_alignment_result>(std::move(alignments), std::move(str_mapper), projection_vector_);
  });
}

memory::builders::result_column_builder_t alignment_operator::execute(common::execution_context& context) {
  // log_alignment_cache_ is not persisted across queries, so must always call this method.
  //  If it's cached, the overhead is negligible
  auto log_alignment{calculate_log_alignment(table_cache_key_, context)};
  auto alignment_table_size{get_common_table()->get_rows()};

  if (node_.type() == RLAlignOperatorNode::ACTIVITY) {
    return log_alignment->align_activity(alignment_table_size, event_config_->case_id_column->get_row_count(context),
                                         context, variant_column_);
  }
  return log_alignment->align_move(alignment_table_size, event_config_->case_id_column->get_row_count(context), context,
                                   variant_column_);
}

alignment_operator_node::alignment_operator_node(
    operator_node* activity_column_operator_node, operator_node* pruned_activity_column_operator_node,
    operator_node* variant_column_operator_node, operator_node* pruned_variant_column_operator_node,
    log_alignment_result_cache& log_alignment_cache, const RLAlignOperatorNode& node, cube::query_scope& scope,
    log_aligner_config log_aligner_cfg,
    std::optional<cube::execution::tracking::add_telemetry_counter_fn> add_telemetry_counter)
    : activity_column_operator_node_{activity_column_operator_node},
      pruned_activity_column_operator_node_{pruned_activity_column_operator_node},
      variant_column_operator_node_{variant_column_operator_node},
      pruned_variant_column_operator_node_{pruned_variant_column_operator_node},
      log_alignment_cache_{log_alignment_cache},
      node_{node},
      scope_{scope},
      log_aligner_cfg_{std::move(log_aligner_cfg)},
      add_telemetry_counter_{std::move(add_telemetry_counter)} {}

memory::column_t alignment_operator_node::do_execute(cube::execution::tracking::operator_tracker& tracker,
                                                     cube::operator_executor& executor,
                                                     common::execution_context& operator_context) const {
  auto input_cols{execute_input_columns(tracker, executor, operator_context, node_, activity_column_operator_node_,
                                        pruned_activity_column_operator_node_, variant_column_operator_node_,
                                        pruned_variant_column_operator_node_)};

  const auto* event_config{get_event_table_config(input_cols.activity_column->get_owner(), operator_context)};
  const auto* case_table{input_cols.variant_column->get_owner()};
  const auto projection_vector =
      scope_.get_projection_vector(input_cols.activity_column->get_owner(), case_table, operator_context);
  auto& common_table_data{get_or_compute_common_table_data(input_cols, event_config)};

  auto add_telemetry_counter{add_telemetry_counter_.has_value()
                                 ? add_telemetry_counter_.value()
                                 : tracker.telemetry_counter_callback_for(alignment_operator::OPERATOR_KEY)};

  operators::process::alignment::alignment_operator oper{std::move(input_cols.activity_column),
                                                         std::move(input_cols.pruned_activity_column),
                                                         std::move(input_cols.variant_column),
                                                         std::move(input_cols.pruned_variant_column),
                                                         common_table_data,
                                                         projection_vector,
                                                         event_config,
                                                         log_alignment_cache_,
                                                         node_,
                                                         scope_,
                                                         operator_context,
                                                         log_aligner_cfg_,
                                                         std::move(add_telemetry_counter)};
  return executor.cached_execute(oper, operator_context);
}

memory::table* alignment_operator_node::do_compute_common_table(cube::execution::tracking::operator_tracker& tracker,
                                                                cube::operator_executor& executor,
                                                                common::execution_context& operator_context) const {
  auto input_cols{execute_input_columns(tracker, executor, operator_context, node_, activity_column_operator_node_,
                                        pruned_activity_column_operator_node_, variant_column_operator_node_,
                                        pruned_variant_column_operator_node_)};
  auto* activity_column_common_table{
      framework::compute_common_table_or_throw(activity_column_operator_node_, tracker, executor, operator_context)};
  const auto* event_config{get_event_table_config(activity_column_common_table, operator_context)};
  auto& [common_table, _]{get_or_compute_common_table_data(input_cols, event_config)};
  return common_table.get();
}

cube::table_data& alignment_operator_node::get_or_compute_common_table_data(
    const alignment_input_columns& input_cols, const cube::event_table_config* event_config) const {
  auto [table_cache_key, table_configuration_key]{
      build_table_cache_and_configuration_key(event_config->event_table, node_, alignment_operator::OPERATOR_KEY)};

  auto& table_registry{scope_.get_cached_table_registry()};
  auto& alignment_table_data{table_registry.get_table_data(table_cache_key)};
  std::scoped_lock alignment_table_data_lock(alignment_table_data.table_mutex);

  if (alignment_table_data.table == nullptr) {
    memory::user_visible_table_name user_visible_name{
        fmt::format(R"(Temporary table: RL_ALIGN configuration {})", table_configuration_key)};

    alignment_table_data.table = std::make_shared<memory::table>(
        std::nullopt, table_cache_key, common::hash_cache_key(table_cache_key), table_registry.get_swap_info(),
        memory::table_meta_data::make_for_operator_table(), std::move(user_visible_name));
    table_registry.register_schema_parent(alignment_table_data.table, event_config->event_table.get());
    table_registry.register_dependency({input_cols.activity_column, input_cols.pruned_activity_column,
                                        input_cols.variant_column, input_cols.pruned_variant_column},
                                       table_cache_key);
  }

  return alignment_table_data;
}

const cube::event_table_config* alignment_operator_node::get_event_table_config(
    memory::table* table, const common::execution_context& operator_context) const {
  const auto* event_config{cube::get_event_config(table, scope_, operator_context)};
  if (event_config->is_inconsistent) {
    throw common::cpm_exception{
        "The activity table configuration for table [\"{}\"] is inconsistent which blocks the execution of "
        "{}. For more information on the inconsistent entries in the activity table, please check the "
        "warning messages of the Data Model load in Data Integration.",
        event_config->event_table->get_name(), get_user_facing_name(node_)};
  }
  if (event_config->uses_timestamp_based_parallelism_or_subprocesses()) {
    throw common::cpm_exception{"{}: Event logs with sub processes or timestamp-based parallelism are not supported.",
                                get_user_facing_name(node_)};
  }
  return event_config;
}

}  // namespace celonis::accelerator::operators::process::alignment
