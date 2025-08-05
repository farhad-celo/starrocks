#include "mo_bpmn_graph_types.h"

#include <fmt/format.h>

#include "legacy_embedded_ctl/assert.h"
#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/transform/join_projection_factories.h"
#include "modules/operators/aggregation/string_aggregation.h"
#include "modules/operators/mo/mo_bpmn_graph_operator.h"
#include "modules/query/queries.pb.h"

namespace celonis::accelerator::operators::mo {

namespace {

[[nodiscard]] std::string to_string(const mo_bpmn_graph_data_selection_strategy::strategy_t strategy) {
  switch (strategy) {
    case mo_bpmn_graph_data_selection_strategy::frequency:
      return "FREQUENCY";
    case mo_bpmn_graph_data_selection_strategy::set_cover:
      return "SET_COVER";
    case mo_bpmn_graph_data_selection_strategy::object_coverage:
      return "OBJECT_COVERAGE";
    case mo_bpmn_graph_data_selection_strategy::variant_filter:
      return "VARIANT_FILTER";
  }
  legacy_embedded_ctl::assert_unreachable();
}

[[nodiscard]] mo_bpmn_graph_data_selection_strategy::strategy_t extract_strategy_from_query(
    const Query_MOBPMNGraphQueryExpression& query, const Query_MOBPMNGraphComputation& computation) {
  const auto convert_to_operator_strategy{[](const Query_MOBPMNGraphHappyPathStrategy query_strategy) {
    switch (query_strategy) {
      case Query_MOBPMNGraphHappyPathStrategy_FREQUENCY:
        return mo_bpmn_graph_data_selection_strategy::frequency;
      case Query_MOBPMNGraphHappyPathStrategy_SET_COVER:
        return mo_bpmn_graph_data_selection_strategy::set_cover;
      case Query_MOBPMNGraphHappyPathStrategy_OBJECT_COVERAGE:
        return mo_bpmn_graph_data_selection_strategy::object_coverage;
      case Query_MOBPMNGraphHappyPathStrategy_VARIANT_FILTER:
        return mo_bpmn_graph_data_selection_strategy::variant_filter;
      default:
        throw common::internal_exception{
            "MO_BPMN_GRAPH: Happy-path mode has to be either 0 ('FREQUENCY'), 1 ('SET_COVER'), 2 "
            "('OBJECT_COVERAGE') or 3 ('VARIANT_FILTER'), but was {}",
            query_strategy};
    }
  }};

  if (query.has_happy_path_strategy()) {
    return convert_to_operator_strategy(query.happy_path_strategy());
  }
  // The query expression does not have a strategy set, we either use the global strategy (if set) or the default one
  return computation.has_global_happy_path_strategy()
             ? convert_to_operator_strategy(computation.global_happy_path_strategy())
             : mo_bpmn_graph_data_selection_strategy::DEFAULT_STRATEGY;
}

[[nodiscard]] mo_bpmn_graph_data_selection_strategy::complexity_threshold_t extract_complexity_threshold_from_query(
    const Query_MOBPMNGraphQueryExpression& query, const Query_MOBPMNGraphComputation& computation) {
  if (query.has_complexity_threshold()) {
    return query.complexity_threshold();
  }
  // The query expression does not have a threshold set, we either use the global threshold (if set) or the default one
  return computation.has_global_complexity_threshold()
             ? computation.global_complexity_threshold()
             : mo_bpmn_graph_data_selection_strategy::DEFAULT_COMPLEXITY_THRESHOLD;
}

[[nodiscard]] memory::cache::variant_entries_t make_variant_trace_entries(const memory::column_t& activity_column,
                                                                          const memory::column_t& case_column,
                                                                          const common::execution_context& ctx) {
  const auto sub_ctx{ctx.create_sub_context("make_variant_trace_entries", {})};
  /* The activity/case column are not required to be part of a registered event table config e.g., when they are
   * from a temporary eventlog-like table. */
  auto variant_entries{operators::aggregation::generalized_variant_row_ids_computation(
      std::nullopt, memory::transform::case_id_column_to_mapping_and_group_id_domain(case_column, sub_ctx),
      activity_column, sub_ctx)};

  {  // Observability for variant path improvements
    const auto eventlog_size{activity_column->get_row_count(sub_ctx)};
    const auto number_of_cases{case_column->get_domain_count(sub_ctx)};
    const auto number_of_variants{variant_entries->get_num_traces()};
    // Percentage in the range [0, 100) describing how much we 'reduced' the input EL to distinct variants.
    // E.g., if we have 100 cases in our EL and these are distributed over 38 variants, then the reduction is 62%.
    const auto eventlog_reduction_percentage{
        static_cast<int>((1 - static_cast<double>(number_of_variants) / static_cast<double>(number_of_cases)) * 100)};
    log::jinfo(fmt::format("{}: Computed variant entries for variant path optimization.",
                           mo_bpmn_graph_operator::get_user_visible_operator_name()),
               {{"eventlog_size", eventlog_size},
                {"number_of_cases", number_of_cases},
                {"number_of_variants", number_of_variants},
                {"eventlog_reduction_percentage", eventlog_reduction_percentage}});
  }

  return variant_entries;
}

}  // anonymous namespace

mo_bpmn_graph_data_selection_strategy::mo_bpmn_graph_data_selection_strategy()
    : mo_bpmn_graph_data_selection_strategy{DEFAULT_STRATEGY, DEFAULT_COMPLEXITY_THRESHOLD} {}

mo_bpmn_graph_data_selection_strategy::mo_bpmn_graph_data_selection_strategy(
    const strategy_t strategy, const complexity_threshold_t complexity_threshold)
    : strategy_{strategy} {
  if (complexity_threshold < 0.) {
    throw common::cpm_exception{"MO_BPMN_GRAPH: Complexity threshold has to be larger or equal to 0, but is {}.",
                                complexity_threshold};
  }
  if (strategy != variant_filter && complexity_threshold > 1.) {
    throw common::cpm_exception{
        "MO_BPMN_GRAPH: Complexity threshold has to be lower than 1 for the chosen strategy, but is {}.",
        complexity_threshold};
  }
  complexity_threshold_ = complexity_threshold;
}

std::string to_string(const mo_bpmn_graph_data_selection_strategy& selection_strategy_with_threshold) {
  return fmt::format("strategy: [{}], threshold: [{}]", to_string(selection_strategy_with_threshold.strategy()),
                     selection_strategy_with_threshold.complexity_threshold());
}

mo_bpmn_graph_data_selection_strategy extract_mo_bpmn_graph_data_selection_strategy_from_query(
    const Query_MOBPMNGraphQueryExpression& query, const Query_MOBPMNGraphComputation& computation) {
  return {extract_strategy_from_query(query, computation), extract_complexity_threshold_from_query(query, computation)};
}

mo_bpmn_graph_query_expression::mo_bpmn_graph_query_expression(memory::column_t activity_column,
                                                               memory::column_t case_column,
                                                               const common::execution_context& ctx)
    : mo_bpmn_graph_query_expression{std::move(activity_column), std::move(case_column), {}, ctx} {}

mo_bpmn_graph_query_expression::mo_bpmn_graph_query_expression(
    memory::column_t activity_column, memory::column_t case_column,
    const mo_bpmn_graph_data_selection_strategy selection_strategy, const common::execution_context& ctx)
    : activity_column_{std::move(activity_column)},
      case_column_{std::move(case_column)},
      selection_strategy_{selection_strategy} {
  const auto sub_ctx{ctx.create_sub_context("mo_bpmn_graph_query_expression_validation", {})};
  if (activity_column_->get_row_count(sub_ctx) != case_column_->get_row_count(sub_ctx)) {
    throw common::cpm_exception(R"===({}: Mismatching activity column and case column: "{}" and "{}" respectively.)===",
                                mo_bpmn_graph_operator::get_user_visible_operator_name(),
                                activity_column_->get_cache_key(), case_column_->get_cache_key());
  }
}

const memory::column_t& mo_bpmn_graph_query_expression::activity_column() const { return activity_column_; }

const memory::column_t& mo_bpmn_graph_query_expression::case_column() const { return case_column_; }

const mo_bpmn_graph_data_selection_strategy& mo_bpmn_graph_query_expression::selection_strategy() const {
  return selection_strategy_;
}

// Expects an already validated 'mo_bpmn_graph_query_expression'
mo_bpmn_graph_computation_input mo_bpmn_graph_computation_input::from_query_input(
    const mo_bpmn_graph_query_expression& query, const common::execution_context& ctx) {
  const auto& activity_column{query.activity_column()};
  const auto& case_column{query.case_column()};
  const auto& selection_strategy{query.selection_strategy()};
  mo_bpmn_graph_computation_input::eventlog_or_variants_input_t trace_data{};
  if (details::does_strategy_and_threshold_allow_for_variant_based_approach(selection_strategy)) {
    trace_data = variants_input{make_variant_trace_entries(activity_column, case_column, ctx)};
  } else {
    trace_data = eventlog_input{case_column};
  }

  return {activity_column, std::move(trace_data), selection_strategy};
}

std::vector<mo_bpmn_graph_computation_input> mo_bpmn_graph_computation_input::from_query_inputs(
    const std::vector<mo_bpmn_graph_query_expression>& queries, const common::execution_context& ctx) {
  const auto sub_ctx{
      ctx.create_sub_context("query_expressions_to_internal_computation_input", {{"expressions", queries.size()}})};
  mo_bpmn_graph_computation_inputs_t mo_bpmn_graph_computation_inputs{};
  mo_bpmn_graph_computation_inputs.reserve(queries.size());
  // TODO(n.weber): C++23 replace by std::bind_back
  const auto from_query_input_with_bind_back{
      [&sub_ctx](const mo_bpmn_graph_query_expression& query) { return from_query_input(query, sub_ctx); }};
  std::ranges::transform(queries, std::back_inserter(mo_bpmn_graph_computation_inputs),
                         from_query_input_with_bind_back);
  return mo_bpmn_graph_computation_inputs;
}

const memory::column_t& mo_bpmn_graph_computation_input::activity_column() const { return activity_column_; }

bool mo_bpmn_graph_computation_input::has_eventlog_input() const {
  return std::holds_alternative<eventlog_input>(trace_data_);
}

bool mo_bpmn_graph_computation_input::has_variants_input() const {
  return std::holds_alternative<variants_input>(trace_data_);
}

const mo_bpmn_graph_computation_input::eventlog_input& mo_bpmn_graph_computation_input::get_eventlog_input() const {
  common::runtime_assert(has_eventlog_input(), "mo_bpmn_graph_computation_input does not contain eventlog input.");
  return std::get<eventlog_input>(trace_data_);
}

const mo_bpmn_graph_computation_input::variants_input& mo_bpmn_graph_computation_input::get_variants_input() const {
  common::runtime_assert(has_variants_input(), "mo_bpmn_graph_computation_input does not contain variants input.");
  return std::get<variants_input>(trace_data_);
}

const mo_bpmn_graph_data_selection_strategy& mo_bpmn_graph_computation_input::selection_strategy() const {
  return selection_strategy_;
}

mo_bpmn_graph_computation_input::mo_bpmn_graph_computation_input(
    memory::column_t activity_column, eventlog_or_variants_input_t trace_data,
    const mo_bpmn_graph_data_selection_strategy selection_strategy)
    : activity_column_{std::move(activity_column)},
      trace_data_{std::move(trace_data)},
      selection_strategy_{selection_strategy} {}

namespace details {

// TODO(n.weber): We can relax the requirements in a follow up.
bool does_strategy_and_threshold_allow_for_variant_based_approach(
    const mo_bpmn_graph_data_selection_strategy& selection_strategy) {
  switch (selection_strategy.strategy()) {
    case mo_bpmn_graph_data_selection_strategy::set_cover:
      // For now, we only support the variant path for the default strategy/threshold
      return selection_strategy.complexity_threshold() == 1.0;
    default:
      return false;
  }
}

}  // namespace details

}  // namespace celonis::accelerator::operators::mo
