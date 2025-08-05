#pragma once

#include <algorithm>
#include <numeric>
#include <set>
#include <variant>
#include <vector>

#include "legacy_embedded_ctl/utility.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/common/int_types.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/cube/execution/tracking/stop_token_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/row_id.h"
#ifndef CELOSTAR
#include "modules/memory/table_fwd.h"
#include "modules/operators/process/inductive_miner/process_tree_ref.h"
#endif

namespace celonis::accelerator::operators::process {

struct process_tree {
  using count_type = std::size_t;

  struct tau {
    static constexpr std::string_view name{"tau"};
    count_type object_count;

    [[nodiscard]] constexpr bool operator==(const tau& rhs) const noexcept { return object_count == rhs.object_count; }

    [[nodiscard]] constexpr bool operator<(const tau& rhs) const noexcept { return object_count < rhs.object_count; }
  };

  struct activity {
    static constexpr std::string_view name{"activity"};
    using activity_id_type = row_id;
    activity_id_type activity_id{};
    count_type object_count{};

    [[nodiscard]] constexpr bool operator==(const activity& rhs) const noexcept {
      return std::tie(activity_id, object_count) == std::tie(rhs.activity_id, rhs.object_count);
    }

    [[nodiscard]] constexpr bool operator<(const activity& rhs) const noexcept {
      return std::tie(activity_id, object_count) < std::tie(rhs.activity_id, rhs.object_count);
    }
  };

  struct parent {
    std::vector<process_tree> children{};

    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] bool operator==(const parent& rhs) const noexcept { return children == rhs.children; }
  };

  struct exclusive : parent {
    static constexpr std::string_view name{"exclusive"};
    std::vector<count_type> child_object_counts{};

    void recalculate_counts();

    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] bool operator==(const exclusive& rhs) const noexcept;

    [[nodiscard]] bool operator<(const exclusive& rhs) const noexcept;
  };

  struct sequence : parent {
    static constexpr std::string_view name{"sequence"};
    count_type object_count{};

    void recalculate_counts();

    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] bool operator==(const sequence& rhs) const noexcept {
      return parent::operator==(rhs) && object_count == rhs.object_count;
    }

    [[nodiscard]] bool operator<(const sequence& rhs) const noexcept {
      return std::tie(children, object_count) < std::tie(rhs.children, rhs.object_count);
    }
  };

  struct parallel : parent {
    static constexpr std::string_view name{"parallel"};
    count_type object_count{};

    void recalculate_counts();

    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] bool operator==(const parallel& rhs) const noexcept {
      return std::forward_as_tuple(std::set(begin(children), end(children)), object_count) ==
             std::forward_as_tuple(std::set(begin(rhs.children), end(rhs.children)), rhs.object_count);
    }

    [[nodiscard]] bool operator<(const parallel& rhs) const noexcept {
      return std::forward_as_tuple(std::set(begin(children), end(children)), object_count) <
             std::forward_as_tuple(std::set(begin(rhs.children), end(rhs.children)), rhs.object_count);
    }
  };

  struct redo : parent {
    static constexpr std::string_view name{"redo"};
    // invariant: child_redo_counts[0] = redo.object_count + child_redo_counts[1...N]
    count_type object_count{};

    void recalculate_counts();

    [[nodiscard]] bool has_exclusive_child_in_redo() const;

    [[nodiscard]] bool has_tau_child_in_redo() const;

    template <typename F>
    [[nodiscard]] bool any_child_of_redo(const F&& f) const {
      return std::any_of(std::next(begin(children)), end(children), f);
    }

    [[nodiscard]] process_tree do_child_of_redo() const { return *begin(children); }

    template <typename F>
    [[nodiscard]] bool do_child_of_redo(const F&& f) const {
      return f(do_child_of_redo());
    }

    std::vector<count_type> child_redo_counts{};

    // NOLINTNEXTLINE(bugprone-exception-escape)
    [[nodiscard]] bool operator==(const redo& rhs) const noexcept;

    [[nodiscard]] bool operator<(const redo& rhs) const noexcept;
  };

  using node_type = std::variant<tau, activity, exclusive, sequence, parallel, redo>;
  node_type node{};

  // NOLINTNEXTLINE(bugprone-exception-escape)
  [[nodiscard]] constexpr bool operator==(const process_tree& rhs) const noexcept { return node == rhs.node; }

  // NOLINTNEXTLINE(bugprone-exception-escape)
  [[nodiscard]] constexpr bool operator!=(const process_tree& rhs) const noexcept { return !operator==(rhs); }

  // NOLINTNEXTLINE(bugprone-exception-escape)
  [[nodiscard]] constexpr bool operator<(const process_tree& rhs) const noexcept { return node < rhs.node; }

  [[nodiscard]] std::size_t num_children() const;

  [[nodiscard]] const std::vector<process_tree>& get_children() const&;
  [[nodiscard]] std::vector<process_tree> get_children() &&;

  [[nodiscard]] count_type get_object_count() const;

  void set_children(const std::vector<process_tree>& nc);

  [[nodiscard]] const std::vector<count_type>& get_redo_count() const;

  [[nodiscard]] bool is_exclusive_node() const noexcept;

  [[nodiscard]] bool is_sequence_node() const noexcept;

  [[nodiscard]] bool is_parallel_node() const noexcept;

  [[nodiscard]] bool is_redo_node() const noexcept;

  [[nodiscard]] bool is_tau_node() const noexcept;

  [[nodiscard]] bool is_activity_node() const noexcept;

  [[nodiscard]] bool has_exclusive_child() const;

  [[nodiscard]] bool has_parallel_child() const;

  [[nodiscard]] bool has_redo_child() const;

  [[nodiscard]] bool has_sequence_child() const;

  [[nodiscard]] bool has_tau_child() const;

  [[nodiscard]] bool allows_empty_trace() const;

  template <typename F>
  [[nodiscard]] bool any_child_of(const process_tree::node_type& n, F&& f) const {
    return std::visit(
        legacy_embedded_ctl::overloaded{[&f](const exclusive& p) { return std::any_of(begin(p.children), end(p.children), f); },
                        [&f](const redo& p) { return std::any_of(begin(p.children), end(p.children), f); },
                        [&f](const parallel& p) { return std::any_of(begin(p.children), end(p.children), f); },
                        [&f](const sequence& p) { return std::any_of(begin(p.children), end(p.children), f); },
                        [](const auto& /*not used */) { return false; }},
        n);
  }
};

/**
 * Verifies if node counts are consistent, i.e. whether the count invariants hold for each node type as follows:
 *  PAR  node: All children have the same count as the node
 *  SEQ  node: All children have the same count as the node
 *  XOR  node: The node count equals the sum of the child counts
 *  LOOP node: Node count = (do_child_count - SUM(redo_children_counts))
 *
 * Some nodes (XOR, LOOP) maintain a vector with the child counts.
 *  For these nodes we also must ensure that the counts in this vectors are the same as the child counts
 */
[[nodiscard]] bool is_valid_tree(const process_tree& pt);

void minimize(process_tree& pt);

#ifndef CELOSTAR
process_tree_ref convert_to_tables(const process_tree& pt, const memory::column_t& activity_column,
                                   memory::table_row_limit_t table_row_limit, const common::execution_context& context,
                                   const cube::execution::tracking::stop_token& stop_token);
#endif

template <typename>
extern const cel_int_t vertex_code_of;
template <>
constexpr inline cel_int_t vertex_code_of<process_tree::tau>{0};
template <>
constexpr inline cel_int_t vertex_code_of<process_tree::activity>{1};
template <>
constexpr inline cel_int_t vertex_code_of<process_tree::exclusive>{2};
template <>
constexpr inline cel_int_t vertex_code_of<process_tree::sequence>{3};
template <>
constexpr inline cel_int_t vertex_code_of<process_tree::parallel>{4};
template <>
constexpr inline cel_int_t vertex_code_of<process_tree::redo>{5};

[[nodiscard]] constexpr cel_int_t to_vertex_code(const process_tree::node_type& pt_node) {
  return std::visit([](const auto& n) { return vertex_code_of<std::decay_t<decltype(n)>>; }, pt_node);
}

[[nodiscard]] constexpr cel_int_t to_vertex_code(const process_tree& pt) { return to_vertex_code(pt.node); }

size_t tree_size(const process_tree& pt);

size_t count_visible_labels(const process_tree& pt);

size_t count_invisible_labels(const process_tree& pt);

bool contains_seq_xors(const process_tree& pt);

}  // namespace celonis::accelerator::operators::process
