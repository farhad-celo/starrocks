#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "exprs/celonis/agg/variant.h"
#include "modules/common/execution_context.h"
#include "modules/operators/process/inductive_miner/directly_follows_graph.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog_config.h"
#include "util/slice.h"

using testing::ElementsAre;
using testing::Pair;
using testing::UnorderedElementsAre;

namespace celonis::accelerator::operators::process {

namespace {

std::vector<std::pair<row_id, size_t>> extract_start_activities(const directly_follows_graph& dfg) {
    std::vector<std::pair<row_id, size_t>> result;
    std::ranges::transform(dfg[boost::graph_bundle].start_vertices, std::inserter(result, result.begin()),
                           [&dfg](const auto& sv) { return std::make_pair(dfg[sv.first].activity_id, sv.second); });
    return result;
}

std::vector<std::pair<row_id, size_t>> extract_end_activities(const directly_follows_graph& dfg) {
    std::vector<std::pair<row_id, size_t>> result;
    std::ranges::transform(dfg[boost::graph_bundle].end_vertices, std::inserter(result, result.begin()),
                           [&dfg](const auto& ev) { return std::make_pair(dfg[ev.first].activity_id, ev.second); });
    return result;
}

std::vector<std::pair<row_id, size_t>> extract_vertices(const directly_follows_graph& dfg) {
    std::vector<std::pair<row_id, size_t>> result;
    for (auto vp = boost::vertices(dfg); vp.first != vp.second; vp.first++) {
        const auto& vertex = *vp.first;
        result.push_back({dfg[vertex].activity_id, dfg[vertex].count});
    }
    return result;
}

std::vector<std::vector<size_t>> extract_edges(const directly_follows_graph& dfg) {
    // SR uses an old version of matcher which did not support testing::FieldsAre(). So vector is used instead of tuple.
    std::vector<std::vector<size_t>> edges;
    for (auto ep = boost::edges(dfg); ep.first != ep.second; ep.first++) {
        const auto& edge = *ep.first;
        auto source = static_cast<size_t>(dfg[boost::source(edge, dfg)].activity_id);
        auto target = static_cast<size_t>(dfg[boost::target(edge, dfg)].activity_id);
        auto count = dfg[edge].count;
        edges.push_back({source, target, count});
    }
    return edges;
}

} // namespace

class CelonisDirectlyFollowsGraphTest : public testing::Test {
public:
    CelonisDirectlyFollowsGraphTest() = default;

    enum id {
        RESERVED_FOR_NULL, A, B, C, D, E
    };

    void SetUp() override {
        // Assign IDs in advance so that they are consistent across data sets.
        std::vector<std::string> activities = {"A", "B", "C", "D", "E"};
        int index = 1;
        for (const auto& activity: activities) {
            activity_id_map_[activity] = index++;
        }
    }
    void TearDown() override {}

    void add_variant(std::vector<std::string> activities, int count) {
        std::vector<int32_t> variant;
        variant.reserve(activities.size());
        for (const auto& activity: activities) {
            variant.push_back(activity_id_map_[activity]);
        }
        variants_.emplace_back(std::move(variant), count);
    }

private:
    starrocks::Variants variants_;
    std::unordered_map<std::string, int32_t> activity_id_map_;
};

TEST_F(CelonisDirectlyFollowsGraphTest, FilteringNoise) {
    add_variant({"A", "B", "C"}, 8);
    add_variant({"C", "B", "A"}, 1);

    dfg_filter_config filter_config;
    common::execution_context dummy_context;
    size_t grain_size{1024};
    auto miner_config = inductive_miner_config{make_splittable_eventlog_config(variants_, grain_size), dummy_context,
                                               grain_size, filter_config};
    auto dfg{dfg::initialize_dfg(miner_config.eventlog(), dummy_context, miner_config.grain_size())};

    // TODO(j.kim): Use a graph comparison.
    EXPECT_THAT(extract_start_activities(dfg), UnorderedElementsAre(Pair(A, 8), Pair(C, 1)));
    EXPECT_THAT(extract_end_activities(dfg), UnorderedElementsAre(Pair(A, 1), Pair(C, 8)));
    EXPECT_THAT(extract_vertices(dfg), UnorderedElementsAre(Pair(A, 9), Pair(B, 9), Pair(C, 9)));
    EXPECT_THAT(extract_edges(dfg), UnorderedElementsAre(ElementsAre(A, B, 8), ElementsAre(B, C, 8),
                                                         ElementsAre(C, B, 1), ElementsAre(B, A, 1)));

    filter_config.vertices_threshold = 0.2;
    filter_config.edges_threshold = 0.2;

    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].start_vertices, filter_config);
    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].end_vertices, filter_config);
    auto filtered_dfg{dfg::filter_dfg_edges(dfg, filter_config)};

    // Verify filtering
    EXPECT_THAT(extract_start_activities(filtered_dfg), UnorderedElementsAre(Pair(A, 8)));
    EXPECT_THAT(extract_end_activities(filtered_dfg), UnorderedElementsAre(Pair(C, 8)));
    EXPECT_THAT(extract_vertices(filtered_dfg), UnorderedElementsAre(Pair(A, 9), Pair(B, 9), Pair(C, 9)));
    EXPECT_THAT(extract_edges(filtered_dfg), UnorderedElementsAre(ElementsAre(A, B, 8), ElementsAre(B, C, 8)));
}

TEST_F(CelonisDirectlyFollowsGraphTest, FilterInfrequentBehaviorFromInductiveMinerTest) {
    // This is a test trace from InductiveMinerTest.java. But the test has been disabled in cpm-query-engine because
    // splitting eventlog does not work properly with a filter. So it's added here as a filter only test.
    add_variant({"B", "D"}, 5);
    add_variant({"C", "B", "D"}, 5);
    add_variant({"B", "D", "C", "B", "D"}, 1);

    dfg_filter_config filter_config;
    common::execution_context dummy_context;
    size_t grain_size{1024};
    auto miner_config = inductive_miner_config{make_splittable_eventlog_config(variants_, grain_size), dummy_context,
                                               grain_size, filter_config};
    auto dfg{dfg::initialize_dfg(miner_config.eventlog(), dummy_context, miner_config.grain_size())};

    EXPECT_THAT(extract_start_activities(dfg), UnorderedElementsAre(Pair(B, 6), Pair(C, 5)));
    EXPECT_THAT(extract_end_activities(dfg), UnorderedElementsAre(Pair(D, 11)));
    EXPECT_THAT(extract_vertices(dfg), UnorderedElementsAre(Pair(B, 12), Pair(C, 6), Pair(D, 12)));
    EXPECT_THAT(extract_edges(dfg), UnorderedElementsAre(ElementsAre(B, D, 12), ElementsAre(D, C, 1),
                                                         ElementsAre(C, B, 6)));

    filter_config.edges_threshold = 0.8;

    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].start_vertices, filter_config);
    dfg::filter_dfg_count_map(dfg[boost::graph_bundle].end_vertices, filter_config);
    auto filtered_dfg{dfg::filter_dfg_edges(dfg, filter_config)};

    // Verify filtering
    EXPECT_THAT(extract_start_activities(filtered_dfg), UnorderedElementsAre(Pair(B, 6), Pair(C, 5)));
    EXPECT_THAT(extract_end_activities(filtered_dfg), UnorderedElementsAre(Pair(D, 11)));
    EXPECT_THAT(extract_vertices(filtered_dfg), UnorderedElementsAre(Pair(B, 12), Pair(C, 6), Pair(D, 12)));
    EXPECT_THAT(extract_edges(filtered_dfg), UnorderedElementsAre(ElementsAre(B, D, 12), ElementsAre(C, B, 6)));
}

TEST_F(CelonisDirectlyFollowsGraphTest, SimpleFromNoisyXorCutTest) {
    // This is a test trace set from https://github.com/celonis/cpm-query-engine/blob/main/query-engine/src/main/native/cpm-accelerator/test/src/operators/process/inductive_miner/noisy_cut_strategies/noisy_xor_cut_test.cpp
    // It couldn't be used in inductive_miner_test.cpp because the dfg was cut by max_seq_cut somehow before it reaches
    // the filter. So it's added here instead as a filter only test.
    add_variant({"A", "B", "A"}, 2);
    add_variant({"A", "B", "C"}, 1);
    add_variant({"C"}, 1);

    dfg_filter_config filter_config;
    common::execution_context dummy_context;
    size_t grain_size{1024};
    auto miner_config = inductive_miner_config{make_splittable_eventlog_config(variants_, grain_size), dummy_context,
                                               grain_size, filter_config};
    auto dfg{dfg::initialize_dfg(miner_config.eventlog(), dummy_context, miner_config.grain_size())};

    EXPECT_THAT(extract_start_activities(dfg), UnorderedElementsAre(Pair(A, 3), Pair(C, 1)));
    EXPECT_THAT(extract_end_activities(dfg), UnorderedElementsAre(Pair(A, 2), Pair(C, 2)));
    EXPECT_THAT(extract_vertices(dfg), UnorderedElementsAre(Pair(A, 5), Pair(B, 3), Pair(C, 2)));
    EXPECT_THAT(extract_edges(dfg), UnorderedElementsAre(ElementsAre(A, B, 3), ElementsAre(B, A, 2),
                                                         ElementsAre(B, C, 1)));

    filter_config.edges_threshold = 0.9;
    auto filtered_dfg{dfg::filter_dfg_edges(dfg, filter_config)};

    EXPECT_THAT(extract_start_activities(filtered_dfg), UnorderedElementsAre(Pair(A, 3), Pair(C, 1)));
    EXPECT_THAT(extract_end_activities(filtered_dfg), UnorderedElementsAre(Pair(A, 2), Pair(C, 2)));
    EXPECT_THAT(extract_vertices(filtered_dfg), UnorderedElementsAre(Pair(A, 5), Pair(B, 3), Pair(C, 2)));
    EXPECT_THAT(extract_edges(filtered_dfg), UnorderedElementsAre(ElementsAre(A, B, 3), ElementsAre(B, A, 2)));
}

} // namespace celonis::accelerator::operators::process
