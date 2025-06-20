#include "process_tree_to_table.h"

#include <cpml/model/process_tree.h>
#include <cpml/model/pt/node_to_counts_mapping.h>

#include <numeric>
#include <queue>

namespace cpml_proxy {

/** Implementation copied from process_tree_to_table from Saola/InductiveMinerHelper */
namespace {

using cpml::model::process_tree;
using starrocks::celonis::ResultColumn;
using starrocks::celonis::ResultTable;
using starrocks::celonis::NullableResultColumn;

[[nodiscard]] constexpr size_t count_edges(const process_tree& tree) {
    return std::visit(ctl::overloaded{[](const process_tree::activity& /*unused*/) { return size_t{}; },
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

template <typename>
extern const std::int64_t vertex_code_of;
template <>
constexpr inline std::int64_t vertex_code_of<process_tree::tau>{0};
template <>
constexpr inline std::int64_t vertex_code_of<process_tree::activity>{1};
template <>
constexpr inline std::int64_t vertex_code_of<process_tree::exclusive>{2};
template <>
constexpr inline std::int64_t vertex_code_of<process_tree::sequence>{3};
template <>
constexpr inline std::int64_t vertex_code_of<process_tree::parallel>{4};
template <>
constexpr inline std::int64_t vertex_code_of<process_tree::redo>{5};

[[nodiscard]] constexpr std::int64_t to_vertex_code(const process_tree::node_type& pt_node) {
    return std::visit([](const auto& n) { return vertex_code_of<std::decay_t<decltype(n)>>; }, pt_node);
}

[[nodiscard]] constexpr std::int64_t to_vertex_code(const process_tree& pt) {
    return to_vertex_code(pt.node);
}

// Copied and modified from process_tree.cpp.
void fill_result_tables(ResultColumn<int64_t>& vertex_pt_types, NullableResultColumn<int64_t>& vertex_activities,
                        NullableResultColumn<int64_t>& vertex_object_counts, ResultColumn<int64_t>& edge_source_ids,
                        ResultColumn<int64_t>& edge_target_ids, const cpml::model::process_tree& pt) {
    std::queue<const process_tree*> buffer;
    buffer.push(&pt);

    std::int64_t current_vertex_id{0};
    std::int64_t current_edge_id{0};

    while (!buffer.empty()) {
        const auto& current_node{*buffer.front()};

        vertex_pt_types[current_vertex_id] = to_vertex_code(current_node);
        std::visit(ctl::overloaded{[&](const process_tree::activity& a) {
                                       vertex_activities[current_vertex_id] = a.activity_id;
                                       // TODO(b.luppes): remove object counts in a follow-up
                                       vertex_object_counts[current_vertex_id] = 0;
                                   },
                                   [&](const process_tree::tau& t) {
                                       vertex_activities.set_null(current_vertex_id);
                                       // TODO(b.luppes): remove object counts in a follow-up
                                       vertex_object_counts[current_vertex_id] = 0;
                                   },
                                   [&](const process_tree::parent& p) {
                                       vertex_activities.set_null(current_vertex_id);
                                       vertex_object_counts.set_null(current_vertex_id);
                                       for (const auto& child : p.children) {
                                           edge_source_ids[current_edge_id] = current_vertex_id;
                                           edge_target_ids[current_edge_id] = static_cast<int64_t>(
                                                   current_vertex_id + buffer.size()); // future `vertex_id` of `child`

                                           ++current_edge_id;
                                           buffer.push(&child);
                                       }
                                   }},
                   current_node.node);

        ++current_vertex_id;
        buffer.pop();
    }
}

} // anonymous namespace

pt_as_tables convert_pt_to_tables(const cpml::model::process_tree& pt) {
    const table_sizes sizes{pt};

    auto vertex_table = std::make_unique<ResultTable>("vertex_properties", sizes.node_size());
    auto& vertex_pt_types = vertex_table->AddColumn<int64_t>("process_tree_type");
    auto& vertex_activities = vertex_table->AddNullableColumn<int64_t>("activity");
    auto& vertex_object_counts = vertex_table->AddNullableColumn<int64_t>("object_count");

    auto edge_table = std::make_unique<ResultTable>("edge_properties", sizes.edge_size());
    auto& edge_source_ids = edge_table->AddColumn<int64_t>("edge_source_id");
    auto& edge_target_ids = edge_table->AddColumn<int64_t>("edge_target_id");

    fill_result_tables(vertex_pt_types, vertex_activities, vertex_object_counts, edge_source_ids, edge_target_ids, pt);

    return {std::move(vertex_table), std::move(edge_table)};
}

} // namespace cpml_proxy
