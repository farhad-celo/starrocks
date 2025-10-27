#include "modules/operators/process/inductive_miner/process_tree.h"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <queue>
#include <type_traits>

#include <boost/graph/graphviz.hpp>

#include "legacy_embedded_ctl/conversion.h"
#ifndef CELOSTAR
#include "legacy_embedded_ctl/static_array.h"
#endif
#include "legacy_embedded_ctl/utility.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types.h"
#include "modules/cube/execution/tracking/stop_token.h"
#ifndef CELOSTAR
#include "modules/memory/column.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/null_flags.h"
#include "modules/memory/table.h"
#endif

namespace celonis::accelerator::operators::process {

size_t process_tree::num_children() const {
  return std::visit(legacy_embedded_ctl::overloaded{[](const redo& p) {
                                                      std::size_t s = p.children.size();
                                                      return s;
                                                    },
                                                    [](const sequence& p) {
                                                      std::size_t s = p.children.size();
                                                      return s;
                                                    },
                                                    [](const parallel& p) {
                                                      std::size_t s = p.children.size();
                                                      return s;
                                                    },
                                                    [](const exclusive& p) {
                                                      std::size_t s = p.children.size();
                                                      return s;
                                                    },
                                                    [](const auto& /*unused*/) {
                                                      std::size_t s{0};
                                                      return s;
                                                    }},
                    node);
}

const std::vector<process_tree>& process_tree::get_children() const& {
  static const std::vector<process_tree> empty_{std::vector<process_tree>()};
  return std::visit(
      legacy_embedded_ctl::overloaded(  // using parentheses instead of braces here to avoid weird formatting by
                                        // clang-format
          [](const redo& p) -> auto& { return p.children; }, [](const parallel& p) -> auto& { return p.children; },
          [](const exclusive& p) -> auto& { return p.children; }, [](const sequence& p) -> auto& { return p.children; },
          [](const auto& /*unused*/) -> auto& { return empty_; }),
      node);
}

std::vector<process_tree> process_tree::get_children() && {
  return std::visit(
      legacy_embedded_ctl::overloaded(  // using parentheses instead of braces here to avoid weird formatting by
                                        // clang-format
          [](redo&& p) { return std::move(p.children); }, [](parallel&& p) { return std::move(p.children); },
          [](exclusive&& p) { return std::move(p.children); }, [](sequence&& p) { return std::move(p.children); },
          [](auto&& /*unused*/) { return std::vector<process_tree>{std::vector<process_tree>()}; }),
      std::move(node));
}

void process_tree::set_children(const std::vector<process_tree>& nc) {
  std::visit(legacy_embedded_ctl::overloaded{[nc](redo& p) {
                                               p.children = nc;
                                               p.recalculate_counts();
                                             },
                                             [nc](parallel& p) {
                                               p.children = nc;
                                               p.recalculate_counts();
                                             },
                                             [nc](exclusive& p) {
                                               p.children = nc;
                                               p.recalculate_counts();
                                             },
                                             [nc](sequence& p) {
                                               p.children = nc;
                                               // Object count of the first child should be equal to the object count of
                                               // the sequence
                                               p.recalculate_counts();
                                             },
                                             [](auto& /*unused*/) {}},
             node);
}

process_tree::count_type process_tree::get_object_count() const {
  return std::visit(
      legacy_embedded_ctl::overloaded{
          [](const redo& p) { return p.object_count; }, [](const parallel& p) { return p.object_count; },
          [](const sequence& p) { return p.object_count; }, [](const activity& p) { return p.object_count; },
          [](const exclusive& p) {
            return std::accumulate(begin(p.child_object_counts), end(p.child_object_counts), count_type{0});
          },
          [](const tau& p) { return p.object_count; },
          [](const auto& /*unused*/) {
            count_type zero{0};
            return zero;
          }},
      node);
}

void process_tree::redo::recalculate_counts() {
  child_redo_counts.resize(children.size());
  std::transform(begin(children), end(children), begin(child_redo_counts),
                 [](const auto& c) { return c.get_object_count(); });
  const auto num_redos{std::accumulate(std::next(begin(child_redo_counts)), end(child_redo_counts), count_type{0})};
  object_count = children.front().get_object_count() - num_redos;
}

void process_tree::parallel::recalculate_counts() { object_count = children.front().get_object_count(); }

void process_tree::exclusive::recalculate_counts() {
  std::vector<size_t> new_object_counts = {};
  for (auto child{children.begin()}; child != children.end(); child++) {
    new_object_counts.push_back(child->get_object_count());
  }
  child_object_counts = std::move(new_object_counts);
}

void process_tree::sequence::recalculate_counts() { object_count = children.front().get_object_count(); }

const std::vector<process_tree::count_type>& process_tree::get_redo_count() const {
  static const std::vector<process_tree::count_type> empty_{std::vector<process_tree::count_type>()};

  return std::visit(legacy_embedded_ctl::overloaded([](const redo& p) -> auto& { return p.child_redo_counts; },
                                                    [](const auto& /*unused*/) -> auto& { return empty_; }),
                    node);
}

bool process_tree::is_exclusive_node() const noexcept { return std::holds_alternative<exclusive>(node); }

bool process_tree::is_sequence_node() const noexcept { return std::holds_alternative<sequence>(node); }

bool process_tree::is_parallel_node() const noexcept { return std::holds_alternative<parallel>(node); }

bool process_tree::is_redo_node() const noexcept { return std::holds_alternative<redo>(node); }

bool process_tree::is_tau_node() const noexcept { return std::holds_alternative<tau>(node); }

bool process_tree::is_activity_node() const noexcept { return std::holds_alternative<activity>(node); }

bool process_tree::redo::has_exclusive_child_in_redo() const {
  return process_tree::redo::any_child_of_redo([](const auto& child) { return child.is_exclusive_node(); });
}

bool process_tree::redo::has_tau_child_in_redo() const {
  return process_tree::redo::any_child_of_redo([](const auto& child) { return child.is_tau_node(); });
}

bool process_tree::has_exclusive_child() const {
  return any_child_of(node, [](const auto& child) { return child.is_exclusive_node(); });
}

bool process_tree::has_parallel_child() const {
  return any_child_of(node, [](const auto& child) { return child.is_parallel_node(); });
}

bool process_tree::has_redo_child() const {
  return any_child_of(node, [](const auto& child) { return child.is_redo_node(); });
}

bool process_tree::has_sequence_child() const {
  return any_child_of(node, [](const auto& child) { return child.is_sequence_node(); });
}

bool process_tree::has_tau_child() const {
  return any_child_of(node, [](const auto& child) { return child.is_tau_node(); });
}

bool process_tree::allows_empty_trace() const {
  return std::visit(legacy_embedded_ctl::overloaded{
                        [](const tau& /*unused*/) { return true; },
                        [](const redo& p) { return p.children.front().allows_empty_trace(); },
                        [](const parallel& p) {
                          return std::all_of(p.children.begin(), p.children.end(),
                                             [](const process_tree& c) { return c.allows_empty_trace(); });
                        },
                        [](const sequence& p) {
                          return std::all_of(p.children.begin(), p.children.end(),
                                             [](const process_tree& c) { return c.allows_empty_trace(); });
                        },
                        [](const exclusive& p) {
                          return std::any_of(p.children.begin(), p.children.end(),
                                             [](const process_tree& c) { return c.allows_empty_trace(); });
                        },
                        [](const activity& /*unused*/) { return false; },
                    },
                    node);
}

namespace {

bool is_tau_node(const process_tree& tree) { return tree.is_tau_node(); }

template <typename ITERATOR, typename UNARY_PREDICATE, typename... REST>
std::tuple<ITERATOR, REST...> remove_simultaneously_if(const ITERATOR first, ITERATOR last, UNARY_PREDICATE pred,
                                                       REST... rest) {
  static_assert(std::is_invocable_r_v<bool, decltype(pred), typename std::iterator_traits<ITERATOR>::value_type>);
  constexpr auto increment{[](auto&&... iterators) { (++iterators, ...); }};
  constexpr auto dereference_as_tuple{[](auto&&... iterators) { return std::forward_as_tuple(*iterators...); }};

  auto it{std::find_if(first, last, pred)};
  const auto offset{std::distance(first, it)};
  (std::advance(rest, offset), ...);

  std::tuple result{it, rest...};
  if (it != last) {
    increment(it, rest...);
    for (; it != last; increment(it, rest...)) {
      if (!pred(*it)) {
        std::apply(dereference_as_tuple, result) = std::move(dereference_as_tuple(it, rest...));
        std::apply(increment, result);
      }
    }
  }
  return result;
}

template <typename PARENT_TYPE>
void collapse_children_of_equal_type(PARENT_TYPE& parent_node) {
  static constexpr auto is_same_type{
      [](const process_tree& tree) { return std::holds_alternative<PARENT_TYPE>(tree.node); }};
  if (std::none_of(begin(parent_node.children), end(parent_node.children), is_same_type)) {
    return;
  }
  static_cast<process_tree::parent&>(parent_node) = std::accumulate(
      begin(parent_node.children), end(parent_node.children), process_tree::parent{}, [&](auto acc, const auto& c) {
        if (is_same_type(c)) {
          auto& child{std::get<PARENT_TYPE>(c.node)};
          acc.children.insert(end(acc.children), begin(child.children), end(child.children));
        } else {
          acc.children.emplace_back(c);
        }
        return acc;
      });
}

template <>
void collapse_children_of_equal_type<process_tree::exclusive>(process_tree::exclusive& parent_node) {
  static constexpr auto is_exclusive{
      [](const process_tree& tree) { return std::holds_alternative<process_tree::exclusive>(tree.node); }};
  if (std::none_of(begin(parent_node.children), end(parent_node.children), is_exclusive)) {
    return;
  }
  parent_node = std::inner_product(
      begin(parent_node.children), end(parent_node.children), begin(parent_node.child_object_counts),
      process_tree::exclusive{},
      [](auto excl, auto p) {
        if (is_exclusive(p.first)) {
          auto& child{std::get<process_tree::exclusive>(p.first.node)};
          excl.children.insert(end(excl.children), begin(child.children), end(child.children));
          excl.child_object_counts.insert(end(excl.child_object_counts), begin(child.child_object_counts),
                                          end(child.child_object_counts));
        } else {
          excl.children.emplace_back(p.first);
          excl.child_object_counts.emplace_back(p.second);
        }
        return excl;
      },
      [](const auto& child, const auto& count) { return std::pair{child, count}; });
}

void minimize_children(process_tree::parent& parent_node) {
  std::for_each(begin(parent_node.children), end(parent_node.children), [](auto& child) { minimize(child); });
}

[[nodiscard]] constexpr size_t count_edges(const process_tree& tree) {
  return std::visit(legacy_embedded_ctl::overloaded{[](const process_tree::activity& /*unused*/) { return size_t{}; },
                                                    [](process_tree::tau /*unused*/) { return size_t{}; },
                                                    [](const process_tree::parent& p) {
                                                      return std::accumulate(
                                                          begin(p.children), end(p.children), p.children.size(),
                                                          [](auto acc, const auto& c) { return acc + count_edges(c); });
                                                    }},
                    tree.node);
}

class table_sizes {
  size_t edge_size_{0};

 public:
  constexpr explicit table_sizes(const process_tree& tree) : edge_size_{count_edges(tree)} {}

  [[nodiscard]] constexpr size_t edge_size() const noexcept { return edge_size_; }

  [[nodiscard]] constexpr size_t node_size() const noexcept { return edge_size_ + 1; }
};

#ifndef CELOSTAR
template <class VERTEX_ACTIVITIES_PTR_AC_TYPE>
void fill_tables(legacy_embedded_ctl::static_array<cel_int_t>& vertex_pt_types,
                 VERTEX_ACTIVITIES_PTR_AC_TYPE& vertex_activities,
                 legacy_embedded_ctl::static_array<cel_int_t>& edge_source_ids,
                 legacy_embedded_ctl::static_array<cel_int_t>& edge_target_ids, const process_tree& pt,
                 const cube::execution::tracking::stop_token& stop_token) {
  std::queue<const process_tree*> buffer;
  buffer.push(&pt);

  row_id current_vertex_id{0};
  row_id current_edge_id{0};

  while (!buffer.empty()) {
    stop_token.stop_execution_if_requested();
    const auto& current_node{*buffer.front()};

    vertex_pt_types[current_vertex_id] = to_vertex_code(current_node);
    auto& current_vertex_activity{vertex_activities[current_vertex_id]};
    using col_pointer_type = typename VERTEX_ACTIVITIES_PTR_AC_TYPE::value_type;
    std::visit(legacy_embedded_ctl::overloaded{
                   [&current_vertex_activity](const process_tree::activity& a) {
                     current_vertex_activity = legacy_embedded_ctl::cast<col_pointer_type>(a.activity_id);
                   },
                   [&current_vertex_activity](const process_tree::tau& /*unused*/) {
                     current_vertex_activity = col_pointer_type{};
                   },
                   [&](const process_tree::parent& p) {
                     current_vertex_activity = col_pointer_type{};
                     for (const auto& child : p.children) {
                       edge_source_ids[current_edge_id] = current_vertex_id;
                       edge_target_ids[current_edge_id] =
                           static_cast<cel_int_t>(current_vertex_id + buffer.size());  // future `vertex_id` of `child`

                       ++current_edge_id;
                       buffer.push(&child);
                     }
                   }},
               current_node.node);

    ++current_vertex_id;
    buffer.pop();
  }
}

struct exec_convert_to_tables {
  row_id num_vertices;
  const process_tree& pt;
  legacy_embedded_ctl::static_array<cel_int_t>& vertex_pt_types;
  legacy_embedded_ctl::static_array<cel_int_t>& edge_source_ids;
  legacy_embedded_ctl::static_array<cel_int_t>& edge_target_ids;
  const common::execution_context& context;
  const cube::execution::tracking::stop_token& stop_token;

  exec_convert_to_tables(const row_id num_vertices, const process_tree& pt,
                         legacy_embedded_ctl::static_array<cel_int_t>& vertex_pt_types,
                         legacy_embedded_ctl::static_array<cel_int_t>& edge_source_ids,
                         legacy_embedded_ctl::static_array<cel_int_t>& edge_target_ids,
                         const common::execution_context& context,
                         const cube::execution::tracking::stop_token& stop_token)
      : num_vertices{num_vertices},
        pt{pt},
        vertex_pt_types{vertex_pt_types},
        edge_source_ids{edge_source_ids},
        edge_target_ids{edge_target_ids},
        context{context},
        stop_token{stop_token} {}

  template <typename COL_PTRS_TYPE>
  memory::raw_column_ptrs_t operator()() {
    auto raw_column_pointers =
        memory::create_raw_column_pointer<COL_PTRS_TYPE>(num_vertices, memory::zero_init_t{false}, context);
    auto vertex_activities_ptrs = raw_column_pointers->get_data();

    fill_tables(vertex_pt_types, vertex_activities_ptrs, edge_source_ids, edge_target_ids, pt, stop_token);

    return raw_column_pointers;
  }
};
#endif

template <typename ITERATOR1, typename ITERATOR2>
auto zip_to_map(ITERATOR1 first1, ITERATOR1 last1, ITERATOR2 first2) {
  using result_type = std::map<typename std::iterator_traits<ITERATOR1>::value_type,
                               typename std::iterator_traits<ITERATOR2>::value_type>;
  result_type result{};
  for (; first1 != last1; ++first1, ++first2) {
    result.emplace(*first1, *first2);
  }
  return result;
}

template <typename CHILD_ITERATOR, typename COUNT_ITERATOR>
auto reduce_to_first_tau(CHILD_ITERATOR first_child, CHILD_ITERATOR last_child, COUNT_ITERATOR first_count) {
  if (const auto first_silent{std::find_if(first_child, last_child, is_tau_node)}; first_silent != last_child) {
    const auto first_silent_count{std::next(first_count, std::distance(first_child, first_silent))};

    const auto reduce_from_child{std::next(first_silent)};
    const auto reduce_from_count{std::next(first_silent_count)};
    *first_silent_count += std::inner_product(
        reduce_from_child, last_child, reduce_from_count, process_tree::count_type{}, std::plus<>{},
        [](const auto& child, auto count) { return is_tau_node(child) ? count : process_tree::count_type{}; });
    std::get<process_tree::tau>(first_silent->node).object_count = *first_silent_count;
    return remove_simultaneously_if(reduce_from_child, last_child, is_tau_node, reduce_from_count);
  }
  return std::tuple{last_child, std::next(first_count, std::distance(first_child, last_child))};
}

}  // namespace

bool is_valid_tree(const process_tree& pt) {
  struct verify_child {
    process_tree::count_type expected_count;

    [[nodiscard]] bool operator()(const process_tree& tree) const {
      return tree.get_object_count() == expected_count && is_valid_tree(tree);
    }
  };

  return std::visit(legacy_embedded_ctl::overloaded{
                        [](const process_tree::sequence& s) {
                          if (s.children.empty()) {
                            return false;
                          }
                          auto node_count{s.object_count};
                          return std::ranges::all_of(s.children, verify_child{node_count});
                        },
                        [](const process_tree::exclusive& e) {
                          if (e.children.empty() || e.children.size() != e.child_object_counts.size()) {
                            return false;
                          }
                          for (size_t child_idx{0}; child_idx < e.children.size(); ++child_idx) {
                            if (!verify_child{e.child_object_counts[child_idx]}(e.children[child_idx])) {
                              return false;
                            }
                          }
                          return true;
                        },
                        [](const process_tree::parallel& p) {
                          if (p.children.empty()) {
                            return false;
                          }
                          auto node_count{p.object_count};
                          return std::ranges::all_of(p.children, verify_child{node_count});
                        },
                        [](const process_tree::redo& r) {
                          if (r.children.size() != r.child_redo_counts.size() || r.children.size() < 2) {
                            return false;
                          }
                          const auto object_count{r.object_count};
                          auto children_it{std::cbegin(r.children)};
                          auto counts_it{std::cbegin(r.child_redo_counts)};

                          const auto do_count{*counts_it};
                          // Verify do part
                          if (!verify_child{do_count}(*children_it)) {
                            return false;
                          }

                          process_tree::count_type redo_sum{0};
                          ++children_it;
                          ++counts_it;
                          for (; children_it != std::cend(r.children) && counts_it != std::cend(r.child_redo_counts);
                               ++children_it, ++counts_it) {
                            if (!verify_child{*counts_it}(*children_it)) {
                              return false;
                            }
                            redo_sum += *counts_it;
                          }

                          return (do_count == object_count + redo_sum);
                        },
                        [](const auto& /*unused activity and tau nodes always valid*/) { return true; }},
                    pt.node);
}

void minimize(process_tree& pt) {
  std::visit(
      legacy_embedded_ctl::overloaded{
          [](process_tree::sequence& s) {
            minimize_children(s);
            collapse_children_of_equal_type(s);
            // there shouldn't be any silent transitions, but let's make sure
            s.children.erase(std::remove_if(begin(s.children), end(s.children), is_tau_node), end(s.children));
          },
          [](process_tree::exclusive& e) {
            minimize_children(e);
            collapse_children_of_equal_type(e);
            // only keep one silent transition
            const auto [child_it, count_it]{
                reduce_to_first_tau(begin(e.children), end(e.children), begin(e.child_object_counts))};
            e.children.erase(child_it, end(e.children));
            e.child_object_counts.erase(count_it, end(e.child_object_counts));
          },
          [](process_tree::parallel& p) {
            minimize_children(p);
            collapse_children_of_equal_type(p);
            p.children.erase(std::remove_if(begin(p.children), end(p.children), is_tau_node), end(p.children));
          },
          [](process_tree::redo& r) {
            minimize_children(r);
            if (is_tau_node(r.children.front())) {
              const auto [child_it, count_it]{remove_simultaneously_if(
                  std::next(begin(r.children)), end(r.children), is_tau_node, std::next(begin(r.child_redo_counts)))};

              r.children.erase(child_it, end(r.children));
              r.child_redo_counts.erase(count_it, end(r.child_redo_counts));
              r.child_redo_counts.front() =
                  std::accumulate(std::next(begin(r.child_redo_counts)), end(r.child_redo_counts), r.object_count);
              std::get<process_tree::tau>(r.children.front().node).object_count = r.child_redo_counts.front();
            } else {
              const auto [child_it, count_it]{reduce_to_first_tau(std::next(begin(r.children)), end(r.children),
                                                                  std::next(begin(r.child_redo_counts)))};

              r.children.erase(child_it, end(r.children));
              r.child_redo_counts.erase(count_it, end(r.child_redo_counts));
              r.child_redo_counts.front() =
                  std::accumulate(std::next(begin(r.child_redo_counts)), end(r.child_redo_counts), r.object_count);
            }
          },
          [](process_tree::tau /*unused*/) { /*leaf*/ }, [](const process_tree::activity& /*unused*/) { /*leaf*/ }},
      pt.node);
}

#ifndef CELOSTAR
process_tree_ref convert_to_tables(const process_tree& pt, const memory::column_t& activity_column,
                                   const memory::table_row_limit_t table_row_limit,
                                   const common::execution_context& context,
                                   const cube::execution::tracking::stop_token& stop_token) {
  process_tree_ref tables{};
  const table_sizes sizes{pt};
  auto vertex_pt_types{legacy_embedded_ctl::make_static_array_for_overwrite<cel_int_t>(
      sizes.node_size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG))};

  auto edge_source_ids{memory::tracking::make_static_array_for_overwrite<cel_int_t>(
      sizes.edge_size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
  auto edge_target_ids{memory::tracking::make_static_array_for_overwrite<cel_int_t>(
      sizes.edge_size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};

  const auto& dictionary{activity_column->get_typed_dict<cel_string_t>(context)};

  tables.vertex_table = memory::table::create_query_scope_table(legacy_embedded_ctl::cast<row_id>(sizes.node_size()),
                                                                "vertex_properties", table_row_limit);
  tables.edge_table = memory::table::create_query_scope_table(legacy_embedded_ctl::cast<row_id>(sizes.edge_size()),
                                                              "edge_properties", table_row_limit);

  const auto vertex_activities_ptrs{memory::create_tmp_column_pointers(memory::execute_with_column_pointers_type(
      exec_convert_to_tables{legacy_embedded_ctl::cast<row_id>(sizes.node_size()), pt, vertex_pt_types, edge_source_ids,
                             edge_target_ids, context, stop_token},
      dictionary->get_size()))};

  tables.vertex_table->add_column<cel_int_t>(
      memory::col_name("PROCESS_TREE_TYPE"), memory::col_id("PROCESS_TREE_TYPE"), std::move(vertex_pt_types),
      memory::create_null_flags(sizes.node_size(), context), memory::column_processing_state(), table_row_limit);
  tables.vertex_table->add_column_with_dictified_data(
      cel_string, memory::col_name("ACTIVITY"), memory::col_id("ACTIVITY"), memory::col_cache_key(""),
      vertex_activities_ptrs, dictionary, memory::column_processing_state(), table_row_limit);

  tables.edge_table->add_column<cel_int_t>(
      memory::col_name("EDGE_SOURCE_ID"), memory::col_id("EDGE_SOURCE_ID"), std::move(edge_source_ids),
      memory::create_null_flags(sizes.edge_size(), context), memory::column_processing_state(), table_row_limit);
  tables.edge_table->add_column<cel_int_t>(
      memory::col_name("EDGE_TARGET_ID"), memory::col_id("EDGE_TARGET_ID"), std::move(edge_target_ids),
      memory::create_null_flags(sizes.edge_size(), context), memory::column_processing_state(), table_row_limit);

  return tables;
}
#endif

// NOLINTNEXTLINE(bugprone-exception-escape)
bool process_tree::exclusive::operator==(const process_tree::exclusive& rhs) const noexcept {
  return zip_to_map(begin(children), end(children), begin(child_object_counts)) ==
         zip_to_map(begin(rhs.children), end(rhs.children), begin(rhs.child_object_counts));
}

bool process_tree::exclusive::operator<(const process_tree::exclusive& rhs) const noexcept {
  return zip_to_map(begin(children), end(children), begin(child_object_counts)) <
         zip_to_map(begin(rhs.children), end(rhs.children), begin(rhs.child_object_counts));
}

// NOLINTNEXTLINE(bugprone-exception-escape)
bool process_tree::redo::operator==(const process_tree::redo& rhs) const noexcept {
  // this first element, the do part, has to be equal, the rest can be in arbitrary order
  return std::forward_as_tuple(
             object_count, children.front(), child_redo_counts.front(),
             zip_to_map(std::next(begin(children)), end(children), std::next(begin(child_redo_counts)))) ==
         std::forward_as_tuple(
             rhs.object_count, rhs.children.front(), child_redo_counts.front(),
             zip_to_map(std::next(begin(rhs.children)), end(rhs.children), std::next(begin(rhs.child_redo_counts))));
}

bool process_tree::redo::operator<(const process_tree::redo& rhs) const noexcept {
  return std::forward_as_tuple(
             object_count, children.front(), child_redo_counts.front(),
             zip_to_map(std::next(begin(children)), end(children), std::next(begin(child_redo_counts)))) <
         std::forward_as_tuple(
             rhs.object_count, rhs.children.front(), child_redo_counts.front(),
             zip_to_map(std::next(begin(rhs.children)), end(rhs.children), std::next(begin(rhs.child_redo_counts))));
}

size_t tree_size(const process_tree& pt) {
  return std::visit(legacy_embedded_ctl::overloaded{
                        [](const process_tree::activity& /*unused*/) { return size_t{1}; },
                        [](process_tree::tau /*unused*/) { return size_t{1}; },
                        [](const process_tree::parent& p) {
                          return std::accumulate(begin(p.children), end(p.children), size_t{1},
                                                 [](auto acc, const auto& child) { return acc + tree_size(child); });
                        }},
                    pt.node);
}

size_t count_visible_labels(const process_tree& pt) {
  return std::visit(legacy_embedded_ctl::overloaded{[](const process_tree::activity& /*unused*/) { return size_t{1}; },
                                                    [](process_tree::tau /*unused*/) { return size_t{}; },
                                                    [](const process_tree::parent& p) {
                                                      return std::accumulate(begin(p.children), end(p.children),
                                                                             size_t{}, [](auto acc, const auto& child) {
                                                                               return acc + count_visible_labels(child);
                                                                             });
                                                    }},
                    pt.node);
}

size_t count_invisible_labels(const process_tree& pt) {
  return std::visit(legacy_embedded_ctl::overloaded{[](process_tree::tau /*unused*/) { return size_t{1}; },
                                                    [](const process_tree::activity& /*unused*/) { return size_t{}; },
                                                    [](const process_tree::parent& p) {
                                                      return std::accumulate(begin(p.children), end(p.children),
                                                                             size_t{}, [](auto acc, const auto& child) {
                                                                               return acc +
                                                                                      count_invisible_labels(child);
                                                                             });
                                                    }},
                    pt.node);
}

bool contains_seq_xors(const process_tree& pt) {
  static constexpr auto is_optional_activity{[](const process_tree& tree) {
    return std::visit(
        legacy_embedded_ctl::overloaded{
            [](const process_tree::exclusive& e) {
              const auto tau_count{static_cast<size_t>(std::count_if(begin(e.children), end(e.children), is_tau_node))};
              return (tau_count + 1 == e.children.size()) &&
                     std::find_if(begin(e.children), end(e.children), [](const auto& child) {
                       return std::holds_alternative<process_tree::activity>(child.node);
                     }) != end(e.children);
            },
            [](const auto& /*unused*/) { return false; }},
        tree.node);
  }};
  return std::visit(legacy_embedded_ctl::overloaded{
                        [](process_tree::tau /*unused*/) { return false; },
                        [](const process_tree::activity& /*unused*/) { return false; },
                        [](const process_tree::parent& p) {
                          return std::any_of(begin(p.children), end(p.children), contains_seq_xors);
                        },
                        [](const process_tree::sequence& s) {
                          if (s.children.size() < 2) {
                            return false;
                          }
                          return std::search_n(begin(s.children), end(s.children), 2, is_optional_activity,
                                               [](const auto& child, const auto& pred) { return pred(child); }) !=
                                     end(s.children) ||
                                 std::any_of(begin(s.children), end(s.children), contains_seq_xors);
                        }},
                    pt.node);
}

}  // namespace celonis::accelerator::operators::process
