#include "replay.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <cpml/model/bpmn/utility.h>
#include <cpml/model/bpmn_graph.h>
#include <ctl/assert.h>
#include <ctl/conversion.h>

#include "modules/memory/column.h"
#include "modules/memory/column_pointers.h"
#include "modules/operators/process/bpmn/a_star/replay.h"
#include "modules/operators/process/bpmn/replay_caches.h"
#include "modules/operators/process/bpmn/replay_result_source_target.h"
#include "modules/operators/process/bpmn/replay_types.h"
#include "modules/operators/process/bpmn/replay_utils.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

// TODO(n.weber): Temporary using decls until code is migrated to CPML
using cpml::model::bpmn_graph;
using cpml::model::bpmn::exclusive_choice;
using cpml::model::bpmn::parallel;
using cpml::model::bpmn::task;
using cpml::model::bpmn::vertex_id_type;
using cpml::model::bpmn::vertex_type;

struct model_and_caches_pair {
  const bpmn_graph& model;
  details::replay_caches& replay_caches;
};

using replay_result_conforms = replay_result_source_target;  // Currently conforms directly uses the source target
                                                             // replay

/**
 * Generates the the result for a trace by using the linear sequence of transitions, representing the path taken by a
 * trace through the BPMN model.
 *
 * @param linearized_transitions linear sequence of transitions, from the 'start' vertex of the BPMN model to the 'end'
 * @param input_trace list of values from the input column corresponding to the replayed trace
 * @param case_table_row_index index into the case the result for this trace should be joined to
 * @param model the bpmn model the case was replayed on
 * @return replay_result_source_target containing both the source and target vertex ids and the column ptrs from the
 * input column
 */
[[nodiscard]] replay_result_source_target generate_trace_result(const transitions_t& linearized_transitions,
                                                                const trace_t& input_trace, row_id case_table_row_index,
                                                                const bpmn_graph& model) {
  // todo(h.ashraf): what do we do when we have empty input models ( START -> END ) and an empty trace? the transitions
  //  would be empty in this case.
  debug_assert(!linearized_transitions.empty(), "MO_BPMN_SOURCE/TARGET: Replayer performed no transitions");
  debug_assert(!input_trace.empty(),
               "MO_BPMN_SOURCE/TARGET: Encountered empty trace while trying to replay eventlog on BPMN model.");

  std::vector<vertex_id_type> source_vertices{};
  std::vector<vertex_id_type> target_vertices{};
  std::vector<row_id> source_col_ptrs{};
  std::vector<row_id> target_col_ptrs{};

  // Transitions happen at nodes, we generate edges so +1.
  // additionally, they do not include the last edge to the end node so +1 again.
  const auto result_size{linearized_transitions.size() + 2};
  source_vertices.reserve(result_size);
  target_vertices.reserve(result_size);
  source_col_ptrs.reserve(result_size);
  target_col_ptrs.reserve(result_size);

  std::unordered_map<vertex_id_type, row_id> current_source_kpi;
  auto input_trace_iter{input_trace.begin()};
  // the start vertex gets the KPI value of the first activity in the trace
  current_source_kpi.emplace(model.single_start_vertex(), *input_trace_iter);

  const auto calculate_target_kpi = [&](vertex_id_type source_id, vertex_id_type target_id) -> row_id {
    return std::visit(ctl::overloaded(
                          [&](const bpmn::exclusive_choice& /**/) {
                            current_source_kpi[target_id] = current_source_kpi.at(source_id);
                            return current_source_kpi.at(target_id);
                          },
                          [&](const bpmn::task& /**/) {
                            const auto current_kpi_value{*input_trace_iter};
                            current_source_kpi[target_id] = current_kpi_value;
                            debug_assert(
                                input_trace_iter != input_trace.end(),
                                "Replayer generated more virtual KPI values than available in the input trace");
                            input_trace_iter++;
                            return current_source_kpi.at(target_id);
                          },
                          [&](const bpmn::parallel& /**/) {
                            const auto& input_vec{model.ingoing_vertices().at(target_id)};
                            const auto max_of_inputs{current_source_kpi[*std::max_element(
                                input_vec.begin(), input_vec.end(),
                                [&kpis = current_source_kpi](auto lhs, auto rhs) { return kpis[lhs] < kpis[rhs]; })]};

                            current_source_kpi[target_id] = max_of_inputs;
                            return max_of_inputs;
                          },
                          [](const vertex_type& /**/) {
                            // There should be no transitions on start / end nodes
                            ctl::assert_unreachable();
                            return row_id{0};
                          }),
                      model.get_vertex(target_id).get_vertex_type());
  };

  for (const auto& transition : linearized_transitions) {
    for (const auto& consumed_token : transition.consumed()) {
      const auto source_vertex{consumed_token.get_source_id()};
      const auto target_vertex{consumed_token.get_target_id()};

      source_vertices.push_back(source_vertex);
      target_vertices.push_back(target_vertex);

      source_col_ptrs.push_back(current_source_kpi.at(source_vertex));
      target_col_ptrs.push_back(calculate_target_kpi(source_vertex, target_vertex));
    }
  }

  // take care of the edge to the end vertex
  source_vertices.push_back(linearized_transitions.back().vertex_id());
  target_vertices.push_back(model.single_end_vertex());

  source_col_ptrs.push_back(target_col_ptrs.back());
  target_col_ptrs.push_back(target_col_ptrs.back());  // end node gets values of last task node

  debug_assert(source_vertices.size() == target_vertices.size() && target_vertices.size() == source_col_ptrs.size() &&
               source_col_ptrs.size() == target_col_ptrs.size());

  std::vector<row_id> join_index(source_vertices.size(), case_table_row_index);

  return {std::move(source_vertices), std::move(target_vertices), std::move(source_col_ptrs),
          std::move(target_col_ptrs), std::move(join_index)};
}

/**
 * @return If the case conforms, the returned optional contains a pair of  the set of explored markings with their
 * enabling transitions and the marking on the end node - these can then be used to generate the linear path that the
 * trace took through the model. Otherwise, the optional is empty.
 */
[[nodiscard]] std::optional<transitions_t> check_conformance(const model_and_caches_pair& model_and_caches,
                                                             const activity_trace_t& trace_activity,
                                                             const marking_t& initial_marking,
                                                             const common::execution_context& context) {
  const auto& [model, caches]{model_and_caches};

  const auto linearized_transitions_or_nothing_found{
      a_star::replay_trace(model_and_caches.model, initial_marking, trace_activity, context)};

  if (std::holds_alternative<a_star::non_conforming_subtrace_t>(linearized_transitions_or_nothing_found)) {
    auto non_conforming_prefix{std::get<a_star::non_conforming_subtrace_t>(linearized_transitions_or_nothing_found)};
    caches.non_conforming_prefix_cache().add(std::move(non_conforming_prefix));
    return std::nullopt;
  }
  if (std::holds_alternative<a_star::non_conforming_variant_t>(linearized_transitions_or_nothing_found)) {
    return std::nullopt;  // in case we don't have a too short non_conforming_variant, we aren't guaranteed a
                          // nonconforming prefix
  }

  return std::get<transitions_t>(linearized_transitions_or_nothing_found);
}

/**
 * Replays a single trace and records the passed edges (giving a path) in the BPMN model as well generates values on the
 * vertices in the path based on trace_input ('virtual KPIs').
 *
 * @param trace_activity Column ptrs of the activity column for this trace.
 * @param trace_input Column ptrs of the input column for this trace.
 * @param case_row_index Row in the case/object table corresponding to trace_activity. This is required so that the
 * result (replay_result_source_target) may be joined to the case/object table.
 */
[[nodiscard]] std::optional<replay_result_source_target> replay_trace(const model_and_caches_pair& model_and_caches,
                                                                      const activity_trace_t& trace_activity,
                                                                      const trace_t& trace_input, row_id case_row_index,
                                                                      const common::execution_context& context) {
  debug_assert(trace_activity.size() == trace_input.size());

  const auto& [model, replay_caches]{model_and_caches};

  if (replay_caches.non_conforming_prefix_cache().contains_prefix_of(trace_activity)) {
    return std::nullopt;  // early return if the trace does not conform
  }

  if (const auto optional_linearized_transition{replay_caches.linearized_transitions_cache().maybe_get(trace_activity)};
      optional_linearized_transition.has_value()) {
    return generate_trace_result(*optional_linearized_transition, trace_input, case_row_index, model);
  }

  const auto initial_marking{get_initial_marking(model)};

  const auto trace_conforms{check_conformance(model_and_caches, trace_activity, initial_marking, context)};

  if (!trace_conforms.has_value()) {
    return std::nullopt;  // trace does not conform
  }

  const auto& linearized_transitions{trace_conforms.value()};
  replay_caches.linearized_transitions_cache().add(trace_activity, linearized_transitions);

  return generate_trace_result(linearized_transitions, trace_input, case_row_index, model);
}

[[nodiscard]] bool trace_conforms(const model_and_caches_pair& model_and_caches, const activity_trace_t& trace_activity,
                                  const common::execution_context& context) {
  const auto& [model, replay_caches]{model_and_caches};

  if (replay_caches.non_conforming_prefix_cache().contains_prefix_of(trace_activity)) {
    return false;  // early return if the trace does not conform
  }

  const auto optional_linearized_transition{replay_caches.linearized_transitions_cache().maybe_get(trace_activity)};
  if (optional_linearized_transition.has_value()) {
    return true;  // early return if the trace does conform
  }

  const auto initial_marking{get_initial_marking(model)};
  const auto trace_conforms{check_conformance(model_and_caches, trace_activity, initial_marking, context)};
  return trace_conforms.has_value();
}
/** Base class shared by the 'exec_replay_for_source_target' and 'exec_replay_for_conformance' */
class exec_replay {
 public:
  exec_replay(const model_and_caches_pair& model_and_caches, const row_id row_count) noexcept
      : model_and_caches_{model_and_caches}, row_count_{row_count} {};

  [[nodiscard]] const model_and_caches_pair& get_model_and_caches() const noexcept { return model_and_caches_; }
  [[nodiscard]] row_id get_row_count() const noexcept { return row_count_; }
  [[nodiscard]] activity_trace_t& get_trace_activity() noexcept { return trace_activity_; }

 private:
  const model_and_caches_pair model_and_caches_;
  const row_id row_count_;
  activity_trace_t trace_activity_{};
};
/** Specifics for source-target replay */
class exec_replay_for_source_target final : public exec_replay {
 public:
  exec_replay_for_source_target(const model_and_caches_pair& model_and_caches, const row_id row_count,
                                const common::execution_context& context) noexcept
      : exec_replay{model_and_caches, row_count}, context_{context} {};

  void handle_previous_trace(row_id case_table_row_index, replay_result_source_target& final_result) {
    if (get_trace_activity().empty()) {
      return;
    }

    const auto trace_result{
        replay_trace(get_model_and_caches(), get_trace_activity(), trace_input_, case_table_row_index, context_)};

    if (trace_result.has_value()) {
      final_result += trace_result.value();
    }

    get_trace_activity().clear();
    trace_input_.clear();
  }

  template <class TUPLE>
  [[nodiscard]] replay_result_source_target operator()(const TUPLE& t) {
    const auto activity_ptrs_ac{std::get<0>(t).get_const_accessor(context_)};
    const auto case_ptrs_ac{std::get<1>(t).get_const_accessor(context_)};
    const auto input_ptrs_ac{std::get<2>(t).get_const_accessor(context_)};
    const auto& activity_case_join_index{std::get<3>(t)};

    replay_result_source_target final_result{};
    row_id previous_case_ptr{0};
    row_id previous_index{0};

    for (row_id index{0}; index < get_row_count(); index++) {
      const auto case_ptr{case_ptrs_ac[index]};

      // Skip null cases
      if (case_ptr == 0) {
        continue;
      }

      if (case_ptr != previous_case_ptr && previous_case_ptr != 0) {
        // This is a new trace. Record the result and reset the setup variables
        handle_previous_trace(activity_case_join_index[previous_index], final_result);
      }

      const auto activity_ptr{activity_ptrs_ac[index]};
      get_trace_activity().emplace_back(activity_ptr);

      const auto input_ptr{input_ptrs_ac[index]};
      trace_input_.emplace_back(input_ptr);

      previous_case_ptr = case_ptr;
      previous_index = index;
    }

    // Handle the last trace
    handle_previous_trace(activity_case_join_index[previous_index], final_result);
    return final_result;
  }

 private:
  trace_t trace_input_{};
  const common::execution_context& context_;
};

/** Specifics for conformance replay */
class exec_replay_for_conformance final : public exec_replay {
 public:
  exec_replay_for_conformance(const model_and_caches_pair& model_and_caches, const row_id row_count,
                              const row_id case_count, const common::execution_context& context) noexcept
      : exec_replay{model_and_caches, row_count},
        conforming_rows_{ctl::cast_unsigned(case_count)},
        context_{context} {};

  void handle_previous_trace(row_id case_table_row_index) {
    const auto trace_result{trace_conforms(get_model_and_caches(), get_trace_activity(), context_)};
    conforming_rows_.set(case_table_row_index, trace_result);
    get_trace_activity().clear();
  }

  template <class TUPLE>
  [[nodiscard]] ctl::dynamic_bitset_t operator()(const TUPLE& t) {
    const auto activity_ptrs_ac{std::get<0>(t).get_const_accessor(context_)};
    const auto case_ptrs_ac{std::get<1>(t).get_const_accessor(context_)};

    row_id previous_case_ptr{0};

    for (row_id index{0}; index < get_row_count(); index++) {
      const auto case_ptr{case_ptrs_ac[index]};

      // Skip null cases
      if (case_ptr == 0) {
        continue;
      }

      if (case_ptr != previous_case_ptr && previous_case_ptr != 0) {
        // This is a new trace. Record the result and reset the setup variables
        handle_previous_trace(previous_case_ptr);
      }

      const auto activity_ptr{activity_ptrs_ac[index]};
      get_trace_activity().emplace_back(activity_ptr);

      previous_case_ptr = case_ptr;
    }

    // Handle the last trace
    handle_previous_trace(previous_case_ptr);
    return conforming_rows_;
  }

 private:
  ctl::dynamic_bitset_t conforming_rows_;
  const common::execution_context& context_;
};

}  // namespace

replay_result_source_target replay_eventlog_for_source_target(
    const bpmn_graph& model, const memory::column_t& input_column, const memory::column_t& activity_column,
    const memory::column_t& case_id_column, const memory::join_projection_vector_t& activity_case_join_index,
    common::execution_context& parent_context) {
  if (!model.is_single_object()) {
    throw common::cpm_exception{"Single object BPMN required for source/target"};
  }
  auto context{parent_context.create_sub_context(
      "replay_eventlog_source_target", {{"model", cpml::model::bpmn::to_dot_pretty(model)},
                                        {"eventlog_row_count", case_id_column->get_row_count(parent_context)}})};
  debug_assert(input_column->get_row_count(context) == activity_column->get_row_count(context));

  details::replay_caches replay_caches{context};
  const model_and_caches_pair model_and_caches{model, replay_caches};

  return memory::cast_execute_column_pointers(
      exec_replay_for_source_target{model_and_caches, activity_column->get_row_count(context), context},
      activity_column->get_column_pointers(context), case_id_column->get_column_pointers(context),
      input_column->get_column_pointers(context), activity_case_join_index);
}

ctl::dynamic_bitset_t replay_eventlog_for_conformance(const bpmn_graph& model, const memory::column_t& activity_column,
                                                      const memory::column_t& case_id_column,
                                                      common::execution_context& parent_context) {
  if (!model.is_single_object()) {
    throw common::cpm_exception{"Single object BPMN required for conformance calculation"};
  }
  auto context{parent_context.create_sub_context(
      "replay_eventlog_conformance", {{"model", cpml::model::bpmn::to_dot_pretty(model)},
                                      {"eventlog_row_count", case_id_column->get_row_count(parent_context)}})};
  details::replay_caches replay_caches{context};
  const model_and_caches_pair model_and_caches{model, replay_caches};

  return memory::cast_execute_column_pointers(
      exec_replay_for_conformance{model_and_caches, activity_column->get_row_count(context),
                                  case_id_column->get_domain_count(context), context},
      activity_column->get_column_pointers(context), case_id_column->get_column_pointers(context));
}

}  // namespace celonis::accelerator::operators::process::bpmn