#include "process_tree_reduction.h"

#include <algorithm>
#include <numeric>
#include <variant>

#include "ctl/assert.h"
#include "format/json/json.h"
#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/cube/execution/tracking/stop_token.h"
#ifndef CELOSTAR
#include "modules/operators/process/dot_format_helper.h"
#endif
#include "modules/operators/process/inductive_miner/process_tree.h"

namespace celonis::accelerator::operators::process::reduction {

bool push_down_tau_count(process_tree& tree, process_tree::count_type tau_count) {
  if (!tree.allows_empty_trace()) {
    return false;
  }
  constexpr auto accepts_empty_word{[](const process_tree& tree) { return tree.allows_empty_trace(); }};
  std::visit(ctl::overloaded{
                 [tau_count](process_tree::tau& t) { t.object_count += tau_count; },
                 [](const process_tree::activity& /*unused*/) { debug_assert(false && "This should be unreachable!"); },
                 [&](process_tree::exclusive& e) {
                   const auto it{std::ranges::find_if(e.children, accepts_empty_word)};
                   debug_assert(it != end(e.children));
                   const auto success{push_down_tau_count(*it, tau_count)};
                   debug_assert(success);  // failure should be guarded by the allows_empty_trace above
                   e.child_object_counts[std::distance(begin(e.children), it)] += tau_count;
                 },
                 [&](process_tree::redo& r) {
                   debug_assert(r.children.front().allows_empty_trace());
                   const auto success{push_down_tau_count(r.children.front(), tau_count)};
                   debug_assert(success);
                   r.object_count += tau_count;
                   r.child_redo_counts.front() += tau_count;
                 },
                 [&](process_tree::parallel& p) {
                   const bool success{std::ranges::all_of(
                       p.children, [&](auto& child) { return push_down_tau_count(child, tau_count); })};
                   debug_assert(success);
                   p.object_count += tau_count;
                 },
                 [&](process_tree::sequence& s) {
                   const bool success{std::ranges::all_of(
                       s.children, [&](auto& child) { return push_down_tau_count(child, tau_count); })};
                   debug_assert(success);
                   s.object_count += tau_count;
                 }},
             tree.node);
  return true;
}

/*
 * Reduction rules are based on the following work:
 * Robust Process Mining with Guarantees by Sander J.J. Leemans (ISBN: 978-90-386-4257-4) Page 114
 */
std::vector<reduction_rules::applicable_fn> get_rules() {
  return {node_with_nested_exclusive_applicable,
          node_with_nested_parallel_applicable,
          node_with_nested_sequence_applicable,
          nested_loop_in_do_part_applicable,
          tau_transition_in_seq_applicable,
          tau_transition_in_parallel_applicable,
          tau_transition_in_xor_applicable,
          tau_transition_in_redo_applicable,
          multiple_tau_transitions_in_redo_applicable,
          empty_loop_applicable,
          single_child_applicable,
          redo_nested_in_parallel_applicable};
}

std::vector<reduction_rules::apply_fn> get_transformations() {
  return {node_with_nested_sequence_apply_if_applicable,
          node_with_nested_parallel_apply_if_applicable,
          node_with_nested_exclusive_apply_if_applicable,
          nested_loop_in_do_part_apply_if_applicable,
          tau_transition_in_seq_apply_if_applicable,
          tau_transition_in_parallel_apply_if_applicable,
          tau_transition_in_xor_apply_if_applicable,
          tau_transition_in_redo_apply_if_applicable,
          multiple_tau_transitions_in_redo_apply_if_applicable,
          empty_loop_apply_if_applicable,
          single_child_apply_if_applicable,
          redo_nested_in_parallel_apply_if_applicable};
}

bool node_with_nested_exclusive_applicable(const process_tree& pt) {
  return pt.is_exclusive_node() && pt.has_exclusive_child();
}

void node_with_nested_exclusive_apply(process_tree& pt) {
  node_with_nested_operator_apply<process_tree::exclusive>(pt);
}

bool node_with_nested_exclusive_apply_if_applicable(process_tree& pt) {
  if (node_with_nested_exclusive_applicable(pt)) {
    node_with_nested_exclusive_apply(pt);
    return true;
  }
  return false;
}

bool node_with_nested_sequence_applicable(const process_tree& pt) {
  return pt.is_sequence_node() && pt.has_sequence_child();
}

void node_with_nested_sequence_apply(process_tree& pt) { node_with_nested_operator_apply<process_tree::sequence>(pt); }

bool node_with_nested_sequence_apply_if_applicable(process_tree& pt) {
  if (node_with_nested_sequence_applicable(pt)) {
    node_with_nested_sequence_apply(pt);
    return true;
  }
  return false;
}

bool node_with_nested_parallel_applicable(const process_tree& pt) {
  return pt.is_parallel_node() && pt.has_parallel_child();
}

void node_with_nested_parallel_apply(process_tree& pt) { node_with_nested_operator_apply<process_tree::parallel>(pt); }

bool node_with_nested_parallel_apply_if_applicable(process_tree& pt) {
  if (node_with_nested_parallel_applicable(pt)) {
    node_with_nested_parallel_apply(pt);
    return true;
  }
  return false;
}

bool nested_loop_in_do_part_applicable(const process_tree& pt) {
  return pt.is_redo_node() && pt.get_children().size() > 1 && pt.get_children().front().is_redo_node();
}

void nested_loop_in_do_part_apply(process_tree& pt) {
  auto& redo{std::get<process_tree::redo>(pt.node)};
  const auto& children{redo.children};

  std::vector<process_tree> new_children{};
  auto child{children.begin()};

  const auto& child_redo{std::get<process_tree::redo>((*child).node)};
  const auto& grand_children = child_redo.children;
  new_children.insert(end(new_children), begin(grand_children), end(grand_children));
  child++;
  new_children.insert(end(new_children), child, end(children));

  redo.children = std::move(new_children);
  redo.recalculate_counts();
}

bool nested_loop_in_do_part_apply_if_applicable(process_tree& pt) {
  if (nested_loop_in_do_part_applicable(pt)) {
    nested_loop_in_do_part_apply(pt);
    return true;
  }
  return false;
}

bool xor_nested_in_redo_applicable(const process_tree& pt) {
  if (pt.is_redo_node() && pt.num_children() > 1) {
    const auto& redo{std::get<process_tree::redo>(pt.node)};
    return redo.has_exclusive_child_in_redo();
  }
  return false;
}

void xor_nested_in_redo_apply(process_tree& pt) {
  auto& redo = std::get<process_tree::redo>(pt.node);
  auto& children{redo.children};
  std::vector<process_tree> new_children{};

  for (auto child{children.begin()}; child != children.end(); child++) {
    if (child->is_exclusive_node() && child != children.begin()) {
      const auto& xor_children{child->get_children()};
      new_children.insert(end(new_children), begin(xor_children), end(xor_children));
    } else {
      new_children.push_back(std::move(*child));
    }
  }

  redo.children = std::move(new_children);
  redo.recalculate_counts();
}
/*
 * EML-2787 Has been disabled as we learned that using redo section to model choice was very hard understood by users.
 */
[[maybe_unused]] bool xor_nested_in_redo_apply_if_applicable(process_tree& pt) {
  if (xor_nested_in_redo_applicable(pt)) {
    xor_nested_in_redo_apply(pt);
    return true;
  }
  return false;
}

// Has more than 1 child, where one of them is a tau transition and at least one allows for the empty language
bool tau_transition_in_xor_applicable(const process_tree& pt) {
  if (!(pt.is_exclusive_node() && pt.get_children().size() > 1 && pt.has_tau_child())) {
    return false;
  }
  const auto& node = std::get<process_tree::exclusive>(pt.node);
  const auto& children{node.children};
  return std::any_of(children.begin(), children.end(),
                     [](const auto& c) { return !c.is_tau_node() && c.allows_empty_trace(); });
}

void tau_transition_in_xor_apply(process_tree& pt) {
  auto& node = std::get<process_tree::exclusive>(pt.node);
  const auto& children{node.children};
  std::vector<process_tree> new_children{};
  process_tree::count_type tau_count{0};
  for (auto child : children) {
    if (!child.is_tau_node()) {
      new_children.push_back(std::move(child));
    } else {
      tau_count += child.get_object_count();
    }
  }

  const auto push_to_it{std::ranges::find_if(new_children, [](const auto& c) { return c.allows_empty_trace(); })};
  if (push_to_it == end(new_children)) {
    // TODO (Schumacher)
    throw common::internal_exception{"No subtree found which allows for the empty trace"};
  }
  push_down_tau_count(*push_to_it, tau_count);

  node.children = std::move(new_children);
  node.recalculate_counts();
}

bool tau_transition_in_xor_apply_if_applicable(process_tree& pt) {
  if (tau_transition_in_xor_applicable(pt)) {
    tau_transition_in_xor_apply(pt);
    return true;
  }
  return false;
}

bool tau_transition_in_seq_applicable(const process_tree& pt) {
  return pt.is_sequence_node() && pt.has_tau_child() && pt.get_children().size() > 1;
}

void tau_transition_in_seq_apply(process_tree& pt) { tau_transition_in_operator_apply<process_tree::sequence>(pt); }

bool tau_transition_in_seq_apply_if_applicable(process_tree& pt) {
  if (tau_transition_in_seq_applicable(pt)) {
    tau_transition_in_seq_apply(pt);
    return true;
  }
  return false;
}

bool tau_transition_in_parallel_applicable(const process_tree& pt) {
  return pt.is_parallel_node() && pt.has_tau_child() && pt.get_children().size() > 1;
}

void tau_transition_in_parallel_apply(process_tree& pt) {
  tau_transition_in_operator_apply<process_tree::parallel>(pt);
}

bool tau_transition_in_parallel_apply_if_applicable(process_tree& pt) {
  if (tau_transition_in_parallel_applicable(pt)) {
    tau_transition_in_parallel_apply(pt);
    return true;
  }
  return false;
}

// Loop containing a tau_leaf and tree Q in redo where L(Q) allows for the empty trace
///////
bool tau_transition_in_redo_applicable(const process_tree& pt) {
  if (!pt.is_redo_node() || pt.num_children() <= 1) {
    return false;
  }
  const auto& node = std::get<process_tree::redo>(pt.node);
  const auto tau_in_redo{node.any_child_of_redo([](const auto& c) { return c.is_tau_node(); })};

  const auto empty_trace_in_redo{
      node.any_child_of_redo([](const auto& c) { return !c.is_tau_node() && c.allows_empty_trace(); })};

  return tau_in_redo && empty_trace_in_redo;
}

void tau_transition_in_redo_apply(process_tree& pt) {
  auto& node = std::get<process_tree::redo>(pt.node);
  const auto& children{node.children};

  std::vector<process_tree::count_type> new_child_redo_counts{};
  std::vector<process_tree> new_children{};
  auto child = children.begin();

  // add do part
  new_children.push_back(*child);
  new_child_redo_counts.push_back(child->get_object_count());
  // Now only consider redo part
  child++;
  process_tree::count_type tau_count{0};
  for (; child != children.end(); child++) {
    if (!child->is_tau_node()) {
      new_children.push_back(*child);
      new_child_redo_counts.push_back(child->get_object_count());
    } else {
      tau_count += child->get_object_count();
    }
  }
  // From the applicable function we know that there was a tau child and another subtree which allows for the empty
  // trace We have to add the count from the reduced taus to any child which allows empty traces
  const auto push_to_it{std::ranges::find_if(next(begin(new_children)), end(new_children),
                                             [](const auto& c) { return c.allows_empty_trace(); })};
  if (push_to_it == end(new_children)) {
    // TODO (Schumacher)
    throw common::internal_exception{"No subtree found which allows for the empty trace"};
  }

  push_down_tau_count(*push_to_it, tau_count);
  node.children = std::move(new_children);
  node.child_redo_counts = std::move(new_child_redo_counts);
  node.recalculate_counts();
}

bool tau_transition_in_redo_apply_if_applicable(process_tree& pt) {
  if (tau_transition_in_redo_applicable(pt)) {
    tau_transition_in_redo_apply(pt);
    return true;
  }
  return false;
}

bool multiple_tau_transitions_in_redo_applicable(const process_tree& pt) {
  if (!pt.is_redo_node() || pt.num_children() <= 2) {
    return false;
  }
  const auto& node = std::get<process_tree::redo>(pt.node);
  auto tau_children_redo{
      std::count_if(next(begin(node.children)), end(node.children), [](auto& child) { return child.is_tau_node(); })};
  return tau_children_redo > 1;
}

void multiple_tau_transitions_in_redo_apply(process_tree& pt) {
  auto& node = std::get<process_tree::redo>(pt.node);
  const auto& children{node.children};

  std::vector<process_tree::count_type> new_child_redo_counts{};
  std::vector<process_tree> new_children{};
  auto child = children.begin();

  // add do part
  new_children.push_back(*child);
  new_child_redo_counts.push_back(child->get_object_count());
  // only consider redo part
  child++;
  process_tree::count_type tau_count{0};
  for (; child != children.end(); child++) {
    if (!child->is_tau_node()) {
      new_children.push_back(*child);
      new_child_redo_counts.push_back(child->get_object_count());
    } else {
      tau_count += child->get_object_count();
    }
  }

  // Added merged tau with sum of merged tau counts
  new_children.push_back({process_tree::tau{tau_count}});
  new_child_redo_counts.push_back(tau_count);

  node.children = std::move(new_children);
  node.child_redo_counts = std::move(new_child_redo_counts);
  node.recalculate_counts();
}

bool multiple_tau_transitions_in_redo_apply_if_applicable(process_tree& pt) {
  if (multiple_tau_transitions_in_redo_applicable(pt)) {
    multiple_tau_transitions_in_redo_apply(pt);
    return true;
  }
  return false;
}

bool redo_nested_in_xor_applicable(const process_tree& pt) {
  if (!pt.is_exclusive_node() || pt.num_children() != 2 || !pt.has_redo_child() || !pt.has_tau_child()) {
    return false;
  }

  const auto& node = std::get<process_tree::exclusive>(pt.node);
  return std::any_of(node.children.begin(), node.children.end(), [](const auto& c) {
    if (c.is_redo_node()) {
      const process_tree::redo& redo = std::get<process_tree::redo>(c.node);
      if (redo.children.size() != 2) {
        return false;
      }

      return redo.has_tau_child_in_redo();
    }
    return false;
  });
}

void redo_nested_in_xor_apply(process_tree& pt) {
  const auto& exclusive{std::get<process_tree::exclusive>(pt.node)};
  const auto& children{exclusive.children};

  auto tau_child{children[0]};
  auto redo_child{children[1]};

  if (!tau_child.is_tau_node()) {
    std::swap(tau_child, redo_child);
  }

  const auto& tau_in_xor{std::get<process_tree::tau>(tau_child.node)};
  auto& redo_node{std::get<process_tree::redo>(redo_child.node)};
  debug_assert(redo_node.children.size() == 2);

  const auto& first_redo_child{redo_node.children[0]};
  const auto& tau_redo_child{redo_node.children[1]};
  debug_assert(!first_redo_child.is_tau_node());
  debug_assert(tau_redo_child.is_tau_node());
  const auto& tau_in_redo{std::get<process_tree::tau>(tau_redo_child.node)};

  // The new object counts are:
  // X{(tau:a):a, *[(<...>):b, (tau:c):c]:b-c}:a+b-c => *[(tau:a+2*b-c):, (<...>):b]:a+b-c
  const auto new_tau_count{tau_in_xor.object_count + first_redo_child.get_object_count() * 2 -
                           tau_in_redo.object_count};
  std::vector<process_tree> new_children{{process_tree::tau{new_tau_count}}, first_redo_child};

  // We get rid of the xor and "pull up" the redo x(*()) -> *()
  process_tree new_pt{process_tree::redo{}};
  auto& r{std::get<process_tree::redo>(new_pt.node)};
  r.children = std::move(new_children);
  r.recalculate_counts();
  pt = new_pt;
}
/*
 * EML-2787 Has been disabled as we learned that using redo section to model choice was very hard understood by users.
 */
[[maybe_unused]] bool redo_nested_in_xor_apply_if_applicable(process_tree& pt) {
  if (redo_nested_in_xor_applicable(pt)) {
    redo_nested_in_xor_apply(pt);
    return true;
  }
  return false;
}

bool empty_loop_applicable(const process_tree& pt) {
  if (!pt.is_redo_node()) {
    return false;
  }
  const auto& node = std::get<process_tree::redo>(pt.node);
  const auto& children{node.children};
  return std::all_of(children.begin(), children.end(), [](const process_tree& c) { return c.is_tau_node(); });
}

void empty_loop_apply(process_tree& pt) {
  const auto& redo{std::get<process_tree::redo>(pt.node)};
  auto tau_count{
      std::accumulate(begin(redo.child_redo_counts), end(redo.child_redo_counts), process_tree::count_type{0})};
  process_tree tau{process_tree::tau{tau_count}};
  pt = tau;
}

bool empty_loop_apply_if_applicable(process_tree& pt) {
  if (empty_loop_applicable(pt)) {
    empty_loop_apply(pt);
    return true;
  }
  return false;
}

bool single_child_applicable(const process_tree& pt) {
  if (pt.is_tau_node() || pt.is_activity_node()) {
    return false;
  }
  return pt.num_children() == 1;
}

void single_child_apply(process_tree& pt) {
  // we can't just do pt = pt.get_children().front(), because before the assignment, all of pt's children need to be
  // freed, resulting in a use-after-free on pt.get_children().front().
  // The solution is to copy the child before the assignment:
  auto cp{pt.get_children().front()};
  pt = std::move(cp);
}

bool single_child_apply_if_applicable(process_tree& pt) {
  if (single_child_applicable(pt)) {
    single_child_apply(pt);
    return true;
  }
  return false;
}
/*
 * If there are several redos with a tau in their do part in the same parallel, e.g.
 * PAR(REDO(tau,...),REDO(tau,...),...) we can merge them into a single redo with a tau in the do-part and all of the
 * original re-trees in the re-part. In case all children fulfill this condition this may result in a parallel with a
 * single redo child, which is then reduced by the single child reduction rule.
 */
bool redo_nested_in_parallel_applicable(const process_tree& pt) {
  if (!pt.is_parallel_node() || !pt.has_redo_child()) {
    return false;
  }

  // Applicable if there are at least two redos with a tau in the do part
  for (bool found{false}; const auto& child : pt.get_children()) {
    if (child.is_redo_node() && std::get<process_tree::redo>(child.node).do_child_of_redo().is_tau_node()) {
      if (found) {
        return true;
      }
      found = true;
    }
  }
  return false;
}

void redo_nested_in_parallel_apply(process_tree& pt) {
  auto object_count{pt.get_object_count()};
  std::vector<process_tree::count_type> merged_children_redo_counts = {0};
  std::vector<process_tree> merged_redo_children{{process_tree::tau{0}}};
  std::vector<process_tree> kept_children{};
  for (auto& child : std::move(pt).get_children()) {
    if (child.is_redo_node() && std::get<process_tree::redo>(child.node).do_child_of_redo().is_tau_node()) {
      const auto& redo{std::get<process_tree::redo>(child.node)};
      merged_redo_children.insert(end(merged_redo_children), std::make_move_iterator(next(begin(redo.children))),
                                  std::make_move_iterator(end(redo.children)));
      merged_children_redo_counts.insert(end(merged_children_redo_counts), next(begin(redo.child_redo_counts)),
                                         end(redo.child_redo_counts));
    } else {
      kept_children.push_back(std::move(child));
    }
  }
  auto tau_count{std::accumulate(begin(merged_children_redo_counts), end(merged_children_redo_counts), object_count)};

  merged_redo_children[0] = {process_tree::tau{tau_count}};
  merged_children_redo_counts[0] = tau_count;

  kept_children.push_back(
      process_tree{process_tree::redo{{merged_redo_children}, object_count, {merged_children_redo_counts}}});
  pt = process_tree{process_tree::parallel{{kept_children}, object_count}};
}

bool redo_nested_in_parallel_apply_if_applicable(process_tree& pt) {
  if (redo_nested_in_parallel_applicable(pt)) {
    redo_nested_in_parallel_apply(pt);
    return true;
  }
  return false;
}

bool is_reduced(const process_tree& pt) {
  const auto& children{pt.get_children()};
  const auto& rules{get_rules()};
  return std::all_of(begin(children), end(children), [](auto& c) { return is_reduced(c); }) &&
         std::none_of(begin(rules), end(rules), [pt](const auto& p) { return p(pt); });
}

void reduce_to_normal_form(process_tree& pt, const cube::execution::tracking::stop_token& stop_token,
                           const cel_string_t* dict) {
  for (auto i = 0; i < max_iterations; i++) {
    stop_token.stop_execution_if_requested();
    if (is_reduced(pt)) {
      return;
    }
    stop_token.stop_execution_if_requested();
    reduce_to_normal_form_recurse(pt, dict);
  }
}

void reduce_to_normal_form_recurse(process_tree& pt, const cel_string_t* dict) {
  if (pt.num_children() == 0) {
    return;
  }
  const auto& children{pt.get_children()};
  std::vector<process_tree> new_children{};
  for (auto child : children) {
    reduce_to_normal_form_recurse(child, dict);
    new_children.push_back(child);
  }
  pt.set_children(new_children);
  reduce_node(pt, dict);
}

void reduce_node(process_tree& pt, const cel_string_t* dict) {
  if (pt.num_children() == 0) {
    return;
  }

#ifndef CELOSTAR
  format::json::json_object_t log_details{{"Mined process tree", pt2dot(pt, dict)}};
#endif
  auto transformations{get_transformations()};
  for (size_t i{0}; i < transformations.size(); ++i) {
    const auto& transformation{transformations[i]};

    if (transformation(pt)) {
#ifndef CELOSTAR
      log_details[fmt::format("Process tree after reduction rule {}", i)] = pt2dot(pt, dict);
#endif
    }
  }

#ifndef CELOSTAR
  // TODO(bluppes): CPL-7544 remove eventually
  log::jinfo("Process tree information", log_details);
#endif
}

}  // namespace celonis::accelerator::operators::process::reduction
