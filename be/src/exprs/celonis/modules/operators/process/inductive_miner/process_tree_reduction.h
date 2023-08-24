#pragma once
#include "modules/operators/process/inductive_miner/process_tree.h"

namespace celonis::accelerator::operators::process::reduction {
// associativity reduction rules
bool node_with_nested_exclusive_applicable(const process_tree& pt);
void node_with_nested_exclusive_apply(process_tree& pt);
bool node_with_nested_exclusive_apply_if_applicable(process_tree& pt);

bool node_with_nested_sequence_applicable(const process_tree& pt);
void node_with_nested_sequence_apply(process_tree& pt);
bool node_with_nested_sequence_apply_if_applicable(process_tree& pt);

bool node_with_nested_parallel_applicable(const process_tree& pt);
void node_with_nested_parallel_apply(process_tree& pt);
bool node_with_nested_parallel_apply_if_applicable(process_tree& pt);

bool nested_loop_in_do_part_applicable(const process_tree& pt);
void nested_loop_in_do_part_apply(process_tree& pt);
bool nested_loop_in_do_part_apply_if_applicable(process_tree& pt);

bool xor_nested_in_redo_applicable(const process_tree& pt);
void xor_nested_in_redo_apply(process_tree& pt);
bool xor_nested_in_redo_apply_if_applicable(process_tree& pt);
// tau reduction rules
bool tau_transition_in_xor_applicable(const process_tree& pt);
void tau_transition_in_xor_apply(process_tree& pt);
bool tau_transition_in_xor_apply_if_applicable(process_tree& pt);

bool tau_transition_in_seq_applicable(const process_tree& pt);
void tau_transition_in_seq_apply(process_tree& pt);
bool tau_transition_in_seq_apply_if_applicable(process_tree& pt);

bool tau_transition_in_parallel_applicable(const process_tree& pt);
void tau_transition_in_parallel_apply(process_tree& pt);
bool tau_transition_in_parallel_apply_if_applicable(process_tree& pt);

bool tau_transition_in_redo_applicable(const process_tree& pt);
void tau_transition_in_redo_apply(process_tree& pt);
bool tau_transition_in_redo_apply_if_applicable(process_tree& pt);

bool multiple_tau_transitions_in_redo_applicable(const process_tree& pt);
void multiple_tau_transitions_in_redo_apply(process_tree& pt);
bool multiple_tau_transitions_in_redo_apply_if_applicable(process_tree& pt);

// We did not implement the rule *(tau,...P) => X(tau,*(X(...,P),tau)) because it would create a loop

// This pattern does not exist in the thesis
bool redo_nested_in_xor_applicable(const process_tree& pt);
void redo_nested_in_xor_apply(process_tree& pt);
bool redo_nested_in_xor_apply_if_applicable(process_tree& pt);

bool empty_loop_applicable(const process_tree& pt);
void empty_loop_apply(process_tree& pt);
bool empty_loop_apply_if_applicable(process_tree& pt);

bool single_child_applicable(const process_tree& pt);
void single_child_apply(process_tree& pt);
bool single_child_apply_if_applicable(process_tree& pt);

bool redo_nested_in_parallel_applicable(const process_tree& pt);
void redo_nested_in_parallel_apply(process_tree& pt);
bool redo_nested_in_parallel_apply_if_applicable(process_tree& pt);

bool allows_empty_trace(const process_tree& pt);

struct reduction_rules {
  using applicable_fn = bool (*)(const process_tree&);
  using apply_fn = bool (*)(process_tree&);
};

std::vector<reduction_rules::applicable_fn> get_rules();
std::vector<reduction_rules::apply_fn> get_transformations();

bool is_reduced(const process_tree& pt);
constexpr int max_iterations{1000};
void reduce_to_normal_form_recurse(process_tree& pt, const cel_string_t* dict);
void reduce_to_normal_form(process_tree& pt, const cube::execution::tracking::stop_token& stop_token,
                           const cel_string_t* dict = nullptr);
void reduce_node(process_tree& pt, const cel_string_t* dict);

template <class CONTROL_FLOW_NODE>
void node_with_nested_operator_apply(process_tree& pt) {
  auto& node = std::get<CONTROL_FLOW_NODE>(pt.node);
  auto children{node.children};
  std::vector<process_tree> new_children{};
  for (auto child{children.begin()}; child != children.end(); child++) {
    if (pt.node.index() == child->node.index()) {
      auto grand_children = std::get<CONTROL_FLOW_NODE>(child->node).children;
      new_children.insert(end(new_children), std::move_iterator{begin(grand_children)},
                          std::move_iterator{end(grand_children)});
    } else {
      new_children.push_back(std::move(*child));
    }
  }
  node.children = new_children;
  node.recalculate_counts();
}

template <class CONTROL_FLOW_NODE>
void tau_transition_in_operator_apply(process_tree& pt) {
  auto& node = std::get<CONTROL_FLOW_NODE>(pt.node);
  auto children{node.children};
  children.erase(std::remove_if(begin(children), end(children), [](auto& c) { return c.is_tau_node(); }),
                 end(children));

  // All transitions were tau and got removed, add one
  if (children.empty()) {
    children.emplace_back(process_tree{process_tree::tau{node.object_count}});
  }
  node.children = children;
  node.recalculate_counts();
}

}  // namespace celonis::accelerator::operators::process::reduction