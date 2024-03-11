#include "inductive_miner_helper.h"

#include <chrono>
#include <queue>

#include "exprs/celonis/result_table.h"
#include "exprs/celonis/agg/variant.h"
#include "modules/common/execution_context.h"
#include "modules/cube/execution/tracking/stop_token.h"
#include "modules/operators/process/inductive_miner/directly_follows_graph.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"
#include "modules/operators/process/inductive_miner/inductive_miner_statistics.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_config.h"

using starrocks::celonis::ResultColumn;
using starrocks::celonis::ResultTable;
using starrocks::celonis::NullableResultColumn;

namespace celonis::accelerator::operators::process {

namespace {
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

// Copied and modified from process_tree.cpp.
void fill_result_tables(ResultColumn<int64_t>& vertex_pt_types, NullableResultColumn<int64_t>& vertex_activities,
                        NullableResultColumn<int64_t>& vertex_object_counts, ResultColumn<int64_t>& edge_source_ids,
                        ResultColumn<int64_t>& edge_target_ids, const process_tree& pt) {
    std::queue<const process_tree*> buffer;
    buffer.push(&pt);

    row_id current_vertex_id{0};
    row_id current_edge_id{0};

    while (!buffer.empty()) {
        const auto& current_node{*buffer.front()};

        vertex_pt_types[current_vertex_id] = to_vertex_code(current_node);
        std::visit(ctl::overloaded{[&](const process_tree::activity& a) {
                     vertex_activities[current_vertex_id] = a.activity_id;
                     vertex_object_counts[current_vertex_id] = a.object_count;
                   },
                                   [&](const process_tree::tau& t) {
                                     vertex_activities.set_null(current_vertex_id);
                                     vertex_object_counts[current_vertex_id] = t.object_count;
                                   },
                                   [&](const process_tree::parent& p) {
                                     vertex_activities.set_null(current_vertex_id);
                                     vertex_object_counts.set_null(current_vertex_id);
                                     for (const auto& child : p.children) {
                                         edge_source_ids[current_edge_id] = current_vertex_id;
                                         edge_target_ids[current_edge_id] = static_cast<int64_t>(
                                                 current_vertex_id + buffer.size());  // future `vertex_id` of `child`

                                         ++current_edge_id;
                                         buffer.push(&child);
                                     }
                                   }},
                   current_node.node);

        ++current_vertex_id;
        buffer.pop();
    }
}

std::pair<std::unique_ptr<starrocks::celonis::ResultTable>, std::unique_ptr<starrocks::celonis::ResultTable>>
convert_pt_to_tables(const process_tree& pt) {
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

} // namespace

InductiveMinerHelper::InductiveMinerHelper(const starrocks::Variants& variants, double imfd_frequency_threshold) {
    dfg_filter_config filter_config;
    // TODO(j.kim): Return an error if the threshold is invalid.
    if (imfd_frequency_threshold > 0.0 && imfd_frequency_threshold <= 1.0) {
       filter_config.edges = true;
       filter_config.edges_threshold = imfd_frequency_threshold;
    }
    common::execution_context dummy_context;
    size_t grain_size{1024};
    auto miner_config = inductive_miner_config{make_splittable_eventlog_config(variants, grain_size), dummy_context,
                                               grain_size, filter_config};
    auto dfg{dfg::initialize_dfg(miner_config.eventlog(), dummy_context, miner_config.grain_size())};

    inductive_miner_statistics statistics;
    cube::execution::tracking::stop_token dummy_stop_token{"INDUCTIVE_MINER", std::chrono::minutes(10)};
    process_tree pt = inductive_miner(miner_config, dfg, statistics, dummy_stop_token).tree;

    std::tie(vertex_table_, edge_table_) = convert_pt_to_tables(pt);
    statistics_ = std::move(statistics.data());
}

} // namespace celonis::accelerator::operators::process
