#pragma once

#include <optional>
#include <string>
#include <vector>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_fwd.h"

namespace celonis::accelerator {
class Query_MOBPMNGraphQueryExpression;
class Query_MOBPMNGraphComputation;

namespace operators::mo {

class mo_bpmn_graph_data_selection_strategy {
 public:
  enum happy_path_strategy { frequency, set_cover, object_coverage, variant_filter };
  using strategy_t = happy_path_strategy;
  using complexity_threshold_t = double;

  static constexpr strategy_t DEFAULT_STRATEGY{set_cover};
  static constexpr complexity_threshold_t DEFAULT_COMPLEXITY_THRESHOLD{1.0};

  mo_bpmn_graph_data_selection_strategy();
  mo_bpmn_graph_data_selection_strategy(strategy_t strategy, complexity_threshold_t threshold);

  [[nodiscard]] strategy_t strategy() const { return strategy_; }
  [[nodiscard]] complexity_threshold_t complexity_threshold() const { return complexity_threshold_; }

 private:
  strategy_t strategy_;
  complexity_threshold_t complexity_threshold_;
};

[[nodiscard]] std::string to_string(const mo_bpmn_graph_data_selection_strategy& selection_strategy_with_threshold);

[[nodiscard]] mo_bpmn_graph_data_selection_strategy extract_mo_bpmn_graph_data_selection_strategy_from_query(
    const Query_MOBPMNGraphQueryExpression& query, const Query_MOBPMNGraphComputation& computation);

/** Simple proxy for the query input received via proto. Does some first input validation. */
class mo_bpmn_graph_query_expression {
 public:
  mo_bpmn_graph_query_expression(memory::column_t activity_column, memory::column_t case_column,
                                 const common::execution_context& ctx);
  mo_bpmn_graph_query_expression(memory::column_t activity_column, memory::column_t case_column,
                                 mo_bpmn_graph_data_selection_strategy selection_strategy,
                                 const common::execution_context& ctx);

  [[nodiscard]] const memory::column_t& activity_column() const;
  [[nodiscard]] const memory::column_t& case_column() const;
  [[nodiscard]] const mo_bpmn_graph_data_selection_strategy& selection_strategy() const;

 private:
  memory::column_t activity_column_;
  memory::column_t case_column_;
  mo_bpmn_graph_data_selection_strategy selection_strategy_;
};

using mo_bpmn_graph_query_expressions_t = std::vector<mo_bpmn_graph_query_expression>;

class mo_bpmn_graph_computation_input;
using mo_bpmn_graph_computation_inputs_t = std::vector<mo_bpmn_graph_computation_input>;

/**
 * @brief MO_BPMN_GRAPH internal representation of its query input (@see mo_bpmn_graph_query_expression above)
 * @note This is only exposed (i.e., non-internal to mo_bpmn_graph_operator; @see mo_bpmn_graph_operator.h) as some free
 * functions in mo_bpmn_graph_operator.h have this type as parameter and are exposed for testing.
 */
class mo_bpmn_graph_computation_input {
 public:
  // When using the eventlog path
  struct eventlog_input {
    memory::column_t case_column;
  };
  // When using the variants path
  struct variants_input {
    memory::cache::variant_entries_t variant_entries;
  };
  using eventlog_or_variants_input_t = std::variant<eventlog_input, variants_input>;

  [[nodiscard]] static mo_bpmn_graph_computation_input from_query_input(const mo_bpmn_graph_query_expression& query,
                                                                        const common::execution_context& ctx);
  [[nodiscard]] static mo_bpmn_graph_computation_inputs_t from_query_inputs(
      const mo_bpmn_graph_query_expressions_t& queries, const common::execution_context& ctx);

  [[nodiscard]] const memory::column_t& activity_column() const;
  [[nodiscard]] bool has_eventlog_input() const;
  [[nodiscard]] bool has_variants_input() const;
  [[nodiscard]] const eventlog_input& get_eventlog_input() const;
  [[nodiscard]] const variants_input& get_variants_input() const;
  [[nodiscard]] const mo_bpmn_graph_data_selection_strategy& selection_strategy() const;

 private:
  mo_bpmn_graph_computation_input(memory::column_t activity_column, eventlog_or_variants_input_t trace_data,
                                  mo_bpmn_graph_data_selection_strategy selection_strategy);
  memory::column_t activity_column_;
  eventlog_or_variants_input_t trace_data_;
  mo_bpmn_graph_data_selection_strategy selection_strategy_;
};

namespace details {

/** Returns whether the variant code path is supported for the given mo_bpmn_graph_data_selection_strategy */
[[nodiscard]] bool does_strategy_and_threshold_allow_for_variant_based_approach(
    const mo_bpmn_graph_data_selection_strategy& selection_strategy);

}  // namespace details

}  // namespace operators::mo
}  // namespace celonis::accelerator
