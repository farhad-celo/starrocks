#pragma once

#include <string>
#include <vector>

#include <cpml/model/bpmn_graph_with_block_structure.h>

#include "modules/common/execution_context.h"
#ifndef CELOSTAR
#include "modules/cube/query_scope_fwd.h"
#endif
#include "modules/memory/dictionary_fwd.h"
#include "modules/memory/table_fwd.h"
#ifndef CELOSTAR
#include "modules/operators/mo/mo_bpmn_graph_types.h"
#endif
#include "modules/operators/process/bpmn/bpmn_graph_to_tables.h"
#include "modules/operators/process/inductive_miner/inductive_miner_statistics.h"
#ifdef CELOSTAR
#include "modules/operators/process/inductive_miner/process_tree.h"
#endif

namespace celonis::accelerator::operators::mo {

class mo_bpmn_graph_operator {
 public:
  struct result_type {
    process::bpmn::bpmn_tables tables;
    std::vector<process::inductive_miner_statistics> inductive_miner_statistics{};
  };

#ifdef CELOSTAR
  mo_bpmn_graph_operator(std::vector<process::process_tree> process_trees,
                         std::vector<process::inductive_miner_statistics> statistics,
                         std::vector<memory::column_t> activity_columns,
                         const common::execution_context& parent_context);
#else
  mo_bpmn_graph_operator(const mo_bpmn_graph_query_expressions_t& mo_bpmn_graph_query_expressions,
                         cube::query_scope& scope, const common::execution_context& parent_context);
#endif

  result_type compute() const;

  [[nodiscard]] static std::string get_user_visible_operator_name() noexcept { return "MO_BPMN_GRAPH"; }

 protected:
  struct compute_graph_result {
    cpml::model::bpmn_graph_with_block_structure graph;
    memory::dictionary_t dictionary;
    std::vector<process::inductive_miner_statistics> statistics;
  };
  compute_graph_result compute_graph() const;

 private:
#ifndef CELOSTAR
  struct compute_single_object_graphs_return_type {
    const std::vector<process::bpmn::bpmn_graph_with_block_structure> graphs{};
    const std::vector<process::inductive_miner_statistics> statistics{};
  };

  compute_single_object_graphs_return_type compute_single_object_graphs() const;

  mo_bpmn_graph_computation_inputs_t mo_bpmn_graph_computation_inputs_;
  cube::query_scope& scope_;
#else
  std::vector<process::process_tree> process_trees_;
  std::vector<process::inductive_miner_statistics> statistics_;
  std::vector<memory::column_t> activity_columns_;
#endif
  common::execution_context operator_context_;
};

namespace details {

using bpmn_graph_with_dict = std::pair<cpml::model::bpmn_graph_with_block_structure, memory::dictionary_t>;

/**
 * Merge a set of bpmn graphs for different objects (given their dictionaries).
 *
 * Will do the following steps:
 * * Merge the dictionaries and remap the task ids in the graph
 * * Apply reductions if possible
 * * Overlay the graphs at shared activities
 *
 * @param graphs Vector of bpmn graphs.
 * @param dictionaries Vector of activity dictionaries (paired with a name). Must correspond to the given vector of
 * graphs.
 * @param context Execution context.
 * @return A pair of the merge bpmn graph and the merged activity dictionary.
 */
bpmn_graph_with_dict merge_bpmn_graphs(std::vector<cpml::model::bpmn_graph_with_block_structure> graphs,
                                       const std::vector<std::pair<memory::dictionary_t, std::string>>& dictionaries,
                                       const common::execution_context& context);

}  // namespace details

}  // namespace celonis::accelerator::operators::mo