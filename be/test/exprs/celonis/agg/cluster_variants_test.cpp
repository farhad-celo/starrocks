#include <algorithm>
#include <gtest/gtest.h>

#include <boost/algorithm/string/join.hpp>
#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/anyval_util.h"
#include "../util.h"
#include "gutil/strings/strcat.h"
#include "runtime/runtime_state.h"
#include "testutil/function_utils.h"

namespace starrocks {

namespace {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _func->destroy(_ctx, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext* ctx, const AggregateFunction* func) {
        return std::make_unique<ManagedAggrState>(ctx, func);
    }

    AggDataPtr state() { return _state; }

private:
    ManagedAggrState(FunctionContext* ctx, const AggregateFunction* func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

    FunctionContext* _ctx;
    const AggregateFunction* _func;
    MemPool _mem_pool;
    AggDataPtr _state;
};

} // namespace

class CelonisClusterVariantsTest : public testing::Test {
public:
    CelonisClusterVariantsTest() = default;

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type() {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;
        struct_type.children.emplace_back(celonis::array_type(TYPE_LARGEINT));
        struct_type.children.emplace_back(celonis::array_type(TYPE_BIGINT));
        struct_type.field_names.emplace_back("hash");
        struct_type.field_names.emplace_back("cluster_id");
        return struct_type;
    }

    std::unique_ptr<FunctionContext> get_ctx() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(TYPE_ARRAY),    // variant
                TypeDescriptor::from_logical_type(TYPE_LARGEINT), // hash
                TypeDescriptor::from_logical_type(TYPE_BIGINT),   // min_pts
                TypeDescriptor::from_logical_type(TYPE_BIGINT)    // epsilon
        };
        auto return_type = get_return_type();
        mem_pools_.emplace_back(std::make_unique<MemPool>());
        runtime_states_.emplace_back(std::make_unique<RuntimeState>());
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_context(runtime_states_.back().get(), mem_pools_.back().get(), return_type,
                                                std::move(arg_types)));
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(const std::vector<std::optional<DatumArray>>& variants, const std::vector<int128_t>& hashes,
              int64_t min_pts, int64_t epsilon) {
        auto local_ctx = get_ctx();

        const AggregateFunction* func = get_aggregate_function("celonis_cluster_variants", TYPE_ARRAY, TYPE_STRUCT,
                                                               false);

        const auto size = variants.size();
        Columns columns;
        ColumnPtr variant_column = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
        ColumnPtr hash_column = ColumnHelper::create_column(TypeDescriptor(TYPE_LARGEINT), true);
        for (auto i = 0; i < size; ++i) {
            if (variants[i].has_value()) {
                variant_column->append_datum(variants[i].value());
            } else {
                variant_column->append_nulls(1);
            }
            hash_column->append_datum(hashes[i]);
        }
        columns.push_back(variant_column);
        columns.push_back(hash_column);
        columns.push_back(ColumnHelper::create_const_column<TYPE_BIGINT>(min_pts, size));
        columns.push_back(ColumnHelper::create_const_column<TYPE_BIGINT>(epsilon, size));

        std::vector<ColumnPtr> const_columns;
        std::vector<const Column*> raw_columns;
        for (auto& column: columns) {
            if (column->is_constant()) {
                const_columns.push_back(column);
            } else {
                const_columns.push_back(nullptr);
            }
            raw_columns.push_back(column.get());
        }

        local_ctx->set_constant_columns(std::move(const_columns));

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), size, raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    std::vector<int128_t> ExtractCluster(const std::vector<std::pair<int128_t, int64_t>>& pairs, int64_t label) {
        std::vector<int128_t> hashes;
        for (const auto& entry: pairs) {
            if (entry.second == label) {
                hashes.push_back(entry.first);
            }
        }
        std::sort(hashes.begin(), hashes.end());
        return hashes;
    }

    // Extracts clusters which have label != -1 and label != -2.
    std::vector<std::vector<int128_t>>
    ExtractNormalClusters(const std::vector<std::pair<int128_t, int64_t>>& pairs) {
        phmap::flat_hash_map<int64_t, std::vector<int128_t>> label_to_cluster;
        for (const auto& entry: pairs) {
            const auto hash = entry.first;
            const auto label = entry.second;
            if (label == -1 || label == -2) {
                continue;
            }
            label_to_cluster[label].push_back(hash);
        }
        std::vector<std::vector<int128_t>> clusters;
        for (auto& [label, cluster]: label_to_cluster) {
            if (cluster.empty()) {
                continue;
            }
            std::sort(cluster.begin(), cluster.end());
            clusters.push_back(cluster);
        }
        std::sort(clusters.begin(), clusters.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
        return clusters;
    }

    void ClusterEqual(const std::vector<int128_t>& cluster, const std::vector<int128_t>& expected) {
        ASSERT_EQ(expected.size(), cluster.size());
        for (auto i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(expected[i], cluster[i]);
        }
    }

    void ClustersEqual(const std::vector<std::vector<int128_t>>& clusters,
                       const std::vector<std::vector<int128_t>>& expected) {
        ASSERT_EQ(expected.size(), clusters.size());
        for (auto i = 0; i < expected.size(); ++i) {
            ClusterEqual(expected[i], clusters[i]);
        }
    }

    void Evaluate(Column* result, const std::vector<std::pair<int128_t, int64_t>>& expected) {
        auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(result))->fields_column();
        auto hashes_column = fields[0];
        auto labels_column = fields[1];
        ASSERT_EQ(1, hashes_column->size());
        ASSERT_EQ(1, labels_column->size());
        auto hash_array = hashes_column->get(0).get_array();
        auto label_array = labels_column->get(0).get_array();
        auto const length = expected.size();
        ASSERT_EQ(length, hash_array.size());
        ASSERT_EQ(length, label_array.size());
        std::vector<std::pair<int128_t, int64_t>> hash_label_pairs;
        for (auto i = 0; i < length; ++i) {
            hash_label_pairs.emplace_back(hash_array[i].get_int128(), label_array[i].get_int64());
        }
        // Validate the null cluster
        auto null_cluster = ExtractCluster(hash_label_pairs, -2);
        auto expected_null_cluster = ExtractCluster(expected, -2);
        ClusterEqual(null_cluster, expected_null_cluster);
        // Validate the noise cluster
        auto noise_cluster = ExtractCluster(hash_label_pairs, -1);
        auto expected_noise_cluster = ExtractCluster(expected, -1);
        ClusterEqual(noise_cluster, expected_noise_cluster);
        // Validate the other clusters
        auto normal_clusters = ExtractNormalClusters(hash_label_pairs);
        auto expected_normal_clusters = ExtractNormalClusters(expected);
        ClustersEqual(normal_clusters, expected_normal_clusters);
    }

    void RunMerge(const std::vector<std::optional<DatumArray>>& variants1, const std::vector<int128_t>& hashes1,
                  const std::vector<std::optional<DatumArray>>& variants2, const std::vector<int128_t>& hashes2,
                  int64_t min_pts, int64_t epsilon, const std::vector<std::pair<int128_t, int64_t>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(variants1, hashes1, min_pts, epsilon);
        auto [local_ctx2, state2, func2] = RunUpdate(variants2, hashes2, min_pts, epsilon);

        // Serialize state2
        // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
        auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge state2 into state1
        func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

        // Get the result
        auto result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
        func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

        Evaluate(result.get(), expected);
    }

    void RunMergeToNew(const std::vector<std::optional<DatumArray>>& variants1, const std::vector<int128_t>& hashes1,
                       const std::vector<std::optional<DatumArray>>& variants2, const std::vector<int128_t>& hashes2,
                       int64_t min_pts, int64_t epsilon, const std::vector<std::pair<int128_t, int64_t>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(variants1, hashes1, min_pts, epsilon);
        auto [local_ctx2, state2, func2] = RunUpdate(variants2, hashes2, min_pts, epsilon);
        auto local_ctx3 = get_ctx();

        // Serialize state1 and state2
        // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
        auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge to a new state
        auto state3 = ManagedAggrState::create(local_ctx3.get(), func);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

        // Get the result
        auto result = local_ctx3->create_column(local_ctx3->get_return_type(), false);
        func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

        Evaluate(result.get(), expected);
    }

    void Run(const std::vector<std::optional<DatumArray>>& variants1, const std::vector<int128_t>& hashes1,
             const std::vector<std::optional<DatumArray>>& variants2, const std::vector<int128_t>& hashes2,
             int64_t min_pts, int64_t epsilon, const std::vector<std::pair<int128_t, int64_t>>& expected) {
        RunMerge(variants1, hashes1, variants2, hashes2, min_pts, epsilon, expected);
        RunMergeToNew(variants1, hashes1, variants2, hashes2, min_pts, epsilon, expected);
    }

    void Run(const std::vector<std::pair<DatumArray, int>>& variant_cnt_pairs1,
             const std::vector<std::pair<DatumArray, int>>& variant_cnt_pairs2,
             int64_t min_pts, int64_t epsilon, const std::vector<std::pair<int128_t, int64_t>>& expected) {
        auto variants1 = CreateVariants(variant_cnt_pairs1);
        auto variants2 = CreateVariants(variant_cnt_pairs2);
        std::vector<std::vector<std::optional<DatumArray>>> variant_arrays;
        variant_arrays.push_back(variants1);
        variant_arrays.push_back(variants2);
        auto hash_arrays = CreateHashArrays(variant_arrays);
        auto hashes1 = hash_arrays[0];
        auto hashes2 = hash_arrays[1];
        Run(variants1, hashes1, variants2, hashes2, min_pts, epsilon, expected);
    }

    std::vector<std::optional<DatumArray>> CreateVariants(const std::vector<std::pair<DatumArray, int>>& pairs) {
        std::vector<std::optional<DatumArray>> variants;
        for (const auto& [array, cnt]: pairs) {
            for (auto i = 0; i < cnt; ++i) {
                variants.push_back(array);
            }
        }
        return variants;
    }

    std::vector<std::vector<int128_t>>
    CreateHashArrays(const std::vector<std::vector<std::optional<DatumArray>>>& variant_arrays) {
        std::vector<std::vector<int128_t>> rv;
        phmap::flat_hash_map<std::string, size_t> seen;
        const auto n_arrays = variant_arrays.size();
        for (auto i = 0; i < n_arrays; ++i) {
            std::vector<int128_t> hashes;
            for (const auto& variant: variant_arrays[i]) {
                std::vector<std::string> activities;
                if (variant.has_value()) {
                    for (const auto& activity: variant.value()) {
                        if (activity.is_null()) {
                            continue;
                        }
                        activities.push_back(activity.get_slice().to_string());
                    }
                }
                std::string joined_activities = boost::algorithm::join(activities, ",");
                auto it = seen.find(joined_activities);
                if (it == seen.end()) {
                    seen.insert({joined_activities, seen.size()});
                }
                hashes.push_back(seen[joined_activities]);
            }
            rv.push_back(hashes);
        }
        return rv;
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
    std::vector<std::unique_ptr<RuntimeState>> runtime_states_;
};

TEST_F(CelonisClusterVariantsTest, negative_min_pts) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {};

    Run(variants1, hashes1, variants2, hashes2, -1, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, invalid_epsilon) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {};

    // negative epsilon
    Run(variants1, hashes1, variants2, hashes2, 1, -1, expected);
    // epsilon > 5
    Run(variants1, hashes1, variants2, hashes2, 1, 6, expected);
}

TEST_F(CelonisClusterVariantsTest, cancellation_work) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {};

    int64_t min_pts = 1;
    int64_t epsilon = 2;

    auto [local_ctx1, state1, func] = RunUpdate(variants1, hashes1, min_pts, epsilon);
    auto [local_ctx2, state2, func2] = RunUpdate(variants2, hashes2, min_pts, epsilon);
    auto local_ctx3 = get_ctx();

    // Serialize state1 and state2
    // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
    auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge to a new state
    auto state3 = ManagedAggrState::create(local_ctx3.get(), func);
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

    auto result = local_ctx3->create_column(local_ctx3->get_return_type(), false);
    local_ctx3->state()->set_is_cancelled(true);
    func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());
    ASSERT_TRUE(local_ctx3->has_error());
}

TEST_F(CelonisClusterVariantsTest, null_variant_with_different_hash) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}, DatumArray{kNullDatum, kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5, 7};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        std::nullopt, DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 6, 6};
    std::vector<std::pair<int128_t, int64_t>> expected = {{1, 0},
                                                          {2, 1},
                                                          {3, 0},
                                                          {4, -1},
                                                          {5, -2},
                                                          {6, -2},
                                                          {7, -2}};

    Run(variants1, hashes1, variants2, hashes2, 2, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, null_variant_with_same_hash) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}, DatumArray{kNullDatum, kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        std::nullopt, DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {{1, 0},
                                                          {2, 1},
                                                          {3, 0},
                                                          {4, -1},
                                                          {5, -2}};

    Run(variants1, hashes1, variants2, hashes2, 2, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, two_clusters_and_one_noise_variant) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {{1, 0},
                                                          {2, 1},
                                                          {3, 0},
                                                          {4, -1},
                                                          {5, -2}};

    Run(variants1, hashes1, variants2, hashes2, 2, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, two_clusters_and_two_noise_variants) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {{1, 0},
                                                          {2, -1},
                                                          {3, 0},
                                                          {4, -1},
                                                          {5, -2}};

    Run(variants1, hashes1, variants2, hashes2, 3, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, two_clusters_and_one_noise_variant_lower_epsilon) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {{1, 0},
                                                          {2, 1},
                                                          {3, 0},
                                                          {4, -1},
                                                          {5, -2}};

    Run(variants1, hashes1, variants2, hashes2, 2, 1, expected);
}

TEST_F(CelonisClusterVariantsTest, single_cluster_and_one_noise_variant) {
    std::vector<std::optional<DatumArray>> variants1 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "B", "C"}, DatumArray{"X", "Y", "Z"},
                                                        DatumArray{kNullDatum}};
    std::vector<int128_t> hashes1 = {1, 1, 3, 4, 5};
    std::vector<std::optional<DatumArray>> variants2 = {DatumArray{"A", "B", "C"}, DatumArray{"A", "B", "C"},
                                                        DatumArray{"A", "B", "D"}, DatumArray{"A", "B", "D"},
                                                        DatumArray{}};
    std::vector<int128_t> hashes2 = {1, 1, 2, 2, 5};
    std::vector<std::pair<int128_t, int64_t>> expected = {{1, 0},
                                                          {2, 0},
                                                          {3, 0},
                                                          {4, -1},
                                                          {5, -2}};

    Run(variants1, hashes1, variants2, hashes2, 2, 4, expected);
}

TEST_F(CelonisClusterVariantsTest, single_variant) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"}, 12}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"A", "B", "C", "D"}, 12}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0, 0}};
    Run(pairs1, pairs2, 2, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, empty_input) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {};
    std::vector<std::pair<DatumArray, int>> pairs2 = {};
    std::vector<std::pair<int128_t, int64_t>> expected = {};
    Run(pairs1, pairs2, 2, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, big_data_set_zero_min_pts) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"},           12},
                                                      {DatumArray{"A", "B", "C", "D", "E"},      8},
                                                      {DatumArray{"A", "B", "C"},                10},
                                                      {DatumArray{"A", "B", "B", "C"},           6},
                                                      {DatumArray{"A", "B", "B", "C", "D"},      2},
                                                      {DatumArray{"A", "B", "B", "A", "D"},      1},
                                                      {DatumArray{"A", "B", "B", "A", "C", "D"}, 2},
                                                      {DatumArray{"B", "A", "C", "D"},           9}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"B", "B", "D", "C", "A"}, 13},
                                                      {DatumArray{"G", "F", "H", "I"},      5},
                                                      {DatumArray{"G", "G", "F", "H", "I"}, 2},
                                                      {DatumArray{"G", "F", "H", "H", "I"}, 3},
                                                      {DatumArray{"G", "F", "H", "H"},      4},
                                                      {DatumArray{"G", "F", "I", "H"},      2},
                                                      {DatumArray{"X", "Y", "Z"},           4},
                                                      {DatumArray{"S", "T", "R", "R"},      3},
                                                      {DatumArray{"U", "O", "L", "M"},      5}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  0},
                                                          {1,  0},
                                                          {2,  0},
                                                          {3,  0},
                                                          {4,  0},
                                                          {5,  0},
                                                          {6,  0},
                                                          {7,  0},
                                                          {8,  1},
                                                          {9,  2},
                                                          {10, 2},
                                                          {11, 2},
                                                          {12, 2},
                                                          {13, 2},
                                                          {14, 5},
                                                          {15, 3},
                                                          {16, 4}};
    Run(pairs1, pairs2, 0, 4, expected);
}

TEST_F(CelonisClusterVariantsTest, big_data_set_zero_epsilon) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"},           12},
                                                      {DatumArray{"A", "B", "C", "D", "E"},      8},
                                                      {DatumArray{"A", "B", "C"},                10},
                                                      {DatumArray{"A", "B", "B", "C"},           6},
                                                      {DatumArray{"A", "B", "B", "C", "D"},      2},
                                                      {DatumArray{"A", "B", "B", "A", "D"},      1},
                                                      {DatumArray{"A", "B", "B", "A", "C", "D"}, 2},
                                                      {DatumArray{"B", "A", "C", "D"},           9}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"B", "B", "D", "C", "A"}, 13},
                                                      {DatumArray{"G", "F", "H", "I"},      5},
                                                      {DatumArray{"G", "G", "F", "H", "I"}, 2},
                                                      {DatumArray{"G", "F", "H", "H", "I"}, 3},
                                                      {DatumArray{"G", "F", "H", "H"},      4},
                                                      {DatumArray{"G", "F", "I", "H"},      2},
                                                      {DatumArray{"X", "Y", "Z"},           4},
                                                      {DatumArray{"S", "T", "R", "R"},      3},
                                                      {DatumArray{"U", "O", "L", "M"},      5}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  1},
                                                          {1,  -1},
                                                          {2,  2},
                                                          {3,  -1},
                                                          {4,  -1},
                                                          {5,  -1},
                                                          {6,  -1},
                                                          {7,  -1},
                                                          {8,  0},
                                                          {9,  -1},
                                                          {10, -1},
                                                          {11, -1},
                                                          {12, -1},
                                                          {13, -1},
                                                          {14, -1},
                                                          {15, -1},
                                                          {16, -1}};
    Run(pairs1, pairs2, 10, 0, expected);
}

TEST_F(CelonisClusterVariantsTest, big_data_set_both_parameters_zero) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"},           12},
                                                      {DatumArray{"A", "B", "C", "D", "E"},      8},
                                                      {DatumArray{"A", "B", "C"},                10},
                                                      {DatumArray{"A", "B", "B", "C"},           6},
                                                      {DatumArray{"A", "B", "B", "C", "D"},      2},
                                                      {DatumArray{"A", "B", "B", "A", "D"},      1},
                                                      {DatumArray{"A", "B", "B", "A", "C", "D"}, 2},
                                                      {DatumArray{"B", "A", "C", "D"},           9}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"B", "B", "D", "C", "A"}, 13},
                                                      {DatumArray{"G", "F", "H", "I"},      5},
                                                      {DatumArray{"G", "G", "F", "H", "I"}, 2},
                                                      {DatumArray{"G", "F", "H", "H", "I"}, 3},
                                                      {DatumArray{"G", "F", "H", "H"},      4},
                                                      {DatumArray{"G", "F", "I", "H"},      2},
                                                      {DatumArray{"X", "Y", "Z"},           4},
                                                      {DatumArray{"S", "T", "R", "R"},      3},
                                                      {DatumArray{"U", "O", "L", "M"},      5}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  7},
                                                          {1,  1},
                                                          {2,  15},
                                                          {3,  8},
                                                          {4,  2},
                                                          {5,  3},
                                                          {6,  0},
                                                          {7,  9},
                                                          {8,  4},
                                                          {9,  10},
                                                          {10, 5},
                                                          {11, 6},
                                                          {12, 11},
                                                          {13, 12},
                                                          {14, 16},
                                                          {15, 13},
                                                          {16, 14}};
    Run(pairs1, pairs2, 0, 0, expected);
}

TEST_F(CelonisClusterVariantsTest, big_data_set_three_clusters_one_noise) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"},           12},
                                                      {DatumArray{"A", "B", "C", "D", "E"},      8},
                                                      {DatumArray{"A", "B", "C"},                10},
                                                      {DatumArray{"A", "B", "B", "C"},           6},
                                                      {DatumArray{"A", "B", "B", "C", "D"},      2},
                                                      {DatumArray{"A", "B", "B", "A", "D"},      1},
                                                      {DatumArray{"A", "B", "B", "A", "C", "D"}, 2},
                                                      {DatumArray{"B", "A", "C", "D"},           9}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"B", "B", "D", "C", "A"}, 13},
                                                      {DatumArray{"G", "F", "H", "I"},      5},
                                                      {DatumArray{"G", "G", "F", "H", "I"}, 2},
                                                      {DatumArray{"G", "F", "H", "H", "I"}, 3},
                                                      {DatumArray{"G", "F", "H", "H"},      4},
                                                      {DatumArray{"G", "F", "I", "H"},      2},
                                                      {DatumArray{"X", "Y", "Z"},           4},
                                                      {DatumArray{"S", "T", "R", "R"},      3},
                                                      {DatumArray{"U", "O", "L", "M"},      5}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  0},
                                                          {1,  0},
                                                          {2,  0},
                                                          {3,  0},
                                                          {4,  0},
                                                          {5,  0},
                                                          {6,  0},
                                                          {7,  0},
                                                          {8,  1},
                                                          {9,  2},
                                                          {10, 2},
                                                          {11, 2},
                                                          {12, 2},
                                                          {13, 2},
                                                          {14, -1},
                                                          {15, -1},
                                                          {16, -1}};
    Run(pairs1, pairs2, 10, 4, expected);
}

TEST_F(CelonisClusterVariantsTest, big_data_set_all_noise) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"},           12},
                                                      {DatumArray{"A", "B", "C", "D", "E"},      8},
                                                      {DatumArray{"A", "B", "C"},                10},
                                                      {DatumArray{"A", "B", "B", "C"},           6},
                                                      {DatumArray{"A", "B", "B", "C", "D"},      2},
                                                      {DatumArray{"A", "B", "B", "A", "D"},      1},
                                                      {DatumArray{"A", "B", "B", "A", "C", "D"}, 2},
                                                      {DatumArray{"B", "A", "C", "D"},           9}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"B", "B", "D", "C", "A"}, 13},
                                                      {DatumArray{"G", "F", "H", "I"},      5},
                                                      {DatumArray{"G", "G", "F", "H", "I"}, 2},
                                                      {DatumArray{"G", "F", "H", "H", "I"}, 3},
                                                      {DatumArray{"G", "F", "H", "H"},      4},
                                                      {DatumArray{"G", "F", "I", "H"},      2},
                                                      {DatumArray{"X", "Y", "Z"},           4},
                                                      {DatumArray{"S", "T", "R", "R"},      3},
                                                      {DatumArray{"U", "O", "L", "M"},      5}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  -1},
                                                          {1,  -1},
                                                          {2,  -1},
                                                          {3,  -1},
                                                          {4,  -1},
                                                          {5,  -1},
                                                          {6,  -1},
                                                          {7,  -1},
                                                          {8,  -1},
                                                          {9,  -1},
                                                          {10, -1},
                                                          {11, -1},
                                                          {12, -1},
                                                          {13, -1},
                                                          {14, -1},
                                                          {15, -1},
                                                          {16, -1}};
    Run(pairs1, pairs2, 250, 2, expected);
}

TEST_F(CelonisClusterVariantsTest, exact_min_pts) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"},           1},
                                                      {DatumArray{"A", "B", "C"},                1},
                                                      {DatumArray{"A", "B", "C", "D"},           1},
                                                      {DatumArray{"B", "A", "B", "C"},           1},
                                                      {DatumArray{"B", "C"},                     1},
                                                      {DatumArray{"A", "B"},                     1},
                                                      {DatumArray{"D", "E", "F"},                1},
                                                      {DatumArray{"E", "F"},                     1},
                                                      {DatumArray{"D", "D", "E", "F"},           1},
                                                      {DatumArray{"F", "D", "E", "F"},           1},
                                                      {DatumArray{"D", "E", "E", "E", "E", "F"}, 1},
                                                      {DatumArray{"D", "E"},                     1},
                                                      {DatumArray{"X", "Y", "Z"},                1},
                                                      {DatumArray{"X", "Y", "Y", "Z"},           1},
                                                      {DatumArray{"Y", "Z"},                     1}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"X", "Z", "X", "Y", "Z"},           1},
                                                      {DatumArray{"X", "Y", "Z", "X"},                1},
                                                      {DatumArray{"X", "Y"},                          1},
                                                      {DatumArray{"G", "H", "I"},                     1},
                                                      {DatumArray{"G", "H", "H", "I", "I"},           1},
                                                      {DatumArray{"G", "G", "H"},                     1},
                                                      {DatumArray{"G", "H"},                          1},
                                                      {DatumArray{"H", "I"},                          1},
                                                      {DatumArray{"C", "C", "A", "B"},                1},
                                                      {DatumArray{"D", "F", "D", "F", "D", "D", "D", "E", "F", "D", "F",
                                                                  "E"},                               1},
                                                      {DatumArray{"S", "P", "R", "E", "A", "D"},      1},
                                                      {DatumArray{"L", "O", "W", "Z"},                1},
                                                      {DatumArray{"Z", "A", "B", "Z", "L", "O"},      1},
                                                      {DatumArray{"C", "E", "L", "O", "N", "I", "S"}, 1},
                                                      {DatumArray{"E", "A", "S", "T", "E", "R", "E", "G", "G",
                                                                  "S"},                               1}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  0},
                                                          {1,  0},
                                                          {2,  0},
                                                          {3,  0},
                                                          {4,  0},
                                                          {5,  1},
                                                          {6,  1},
                                                          {7,  1},
                                                          {8,  1},
                                                          {9,  1},
                                                          {10, 1},
                                                          {11, 2},
                                                          {12, 2},
                                                          {13, 2},
                                                          {14, 2},
                                                          {15, 2},
                                                          {16, 2},
                                                          {17, -1},
                                                          {18, -1},
                                                          {19, -1},
                                                          {20, -1},
                                                          {21, -1},
                                                          {22, -1},
                                                          {23, -1},
                                                          {24, -1},
                                                          {25, -1},
                                                          {26, -1},
                                                          {27, -1},
                                                          {28, -1}};
    Run(pairs1, pairs2, 6, 3, expected);
}

TEST_F(CelonisClusterVariantsTest, several_failed_clusters) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "C", "D"},      1},
                                                      {DatumArray{"A", "B", "C"},           1},
                                                      {DatumArray{"A", "B", "C", "D"},      1},
                                                      {DatumArray{"B", "A", "B", "C"},      1},
                                                      {DatumArray{"B", "C"},                1},
                                                      {DatumArray{"A", "B"},                1},
                                                      {DatumArray{"D", "E", "F"},           1},
                                                      {DatumArray{"E", "F"},                1},
                                                      {DatumArray{"X", "Y", "Z"},           1},
                                                      {DatumArray{"G", "H", "I"},           1},
                                                      {DatumArray{"G", "H", "H", "I", "I"}, 1}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"C", "C", "A", "B"},                1},
                                                      {DatumArray{"D", "F", "D", "F", "D", "D", "D", "E", "F", "D", "F",
                                                                  "E"},                               1},
                                                      {DatumArray{"S", "P", "R", "E", "A", "D"},      1},
                                                      {DatumArray{"L", "O", "W", "Z"},                1},
                                                      {DatumArray{"Z", "A", "B", "Z", "L", "O"},      1},
                                                      {DatumArray{"C", "E", "L", "O", "N", "I", "S"}, 1},
                                                      {DatumArray{"E", "A", "S", "T", "E", "R", "E", "G", "G",
                                                                  "S"},                               1}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  0},
                                                          {1,  0},
                                                          {2,  0},
                                                          {3,  0},
                                                          {4,  0},
                                                          {5,  -1},
                                                          {6,  -1},
                                                          {7,  -1},
                                                          {8,  -1},
                                                          {9,  -1},
                                                          {10, -1},
                                                          {11, -1},
                                                          {12, -1},
                                                          {13, -1},
                                                          {14, -1},
                                                          {15, -1},
                                                          {16, -1}};
    Run(pairs1, pairs2, 3, 3, expected);
}

TEST_F(CelonisClusterVariantsTest, smaller_set) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "C"}, 1},
                                                      {DatumArray{"B"},      1},
                                                      {DatumArray{"D", "E"}, 1},
                                                      {DatumArray{"F"},      1},
                                                      {DatumArray{"G", "H"}, 1},
                                                      {DatumArray{"I"},      1},
                                                      {DatumArray{"I"},      1},
                                                      {DatumArray{"I"},      1}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"J", "K"},                1},
                                                      {DatumArray{"J", "K"},                1},
                                                      {DatumArray{"X", "Y", "Z", "U", "P"}, 1},
                                                      {DatumArray{"U", "V", "W", "X"},      1}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0, 0},
                                                          {1, 0},
                                                          {2, 0},
                                                          {3, 0},
                                                          {4, 0},
                                                          {5, 0},
                                                          {6, 0},
                                                          {7, -1},
                                                          {8, -1}};
    Run(pairs1, pairs2, 10, 5, expected);
}

TEST_F(CelonisClusterVariantsTest, significantly_smaller_working_set_than_original_sets) {
    std::vector<std::pair<DatumArray, int>> pairs1 = {{DatumArray{"A", "B", "B", "B", "C"},                     1},
                                                      {DatumArray{"A", "B", "B", "C"},                          1},
                                                      {DatumArray{"A", "B", "B", "B", "B", "C"},                1},
                                                      {DatumArray{"A", "B", "B", "B", "B", "B", "B", "C"},      1},
                                                      {DatumArray{"A", "B", "B", "B", "B", "B", "B", "B", "C"}, 1},
                                                      {DatumArray{"A", "B", "C"},                               1},
                                                      {DatumArray{"A", "A", "B", "C"},                          1},
                                                      {DatumArray{"A", "B", "C", "C"},                          1}};
    std::vector<std::pair<DatumArray, int>> pairs2 = {{DatumArray{"A", "C"},                                         1},
                                                      {DatumArray{"D", "E", "E", "F", "E", "E", "G"},                1},
                                                      {DatumArray{"D", "E", "E", "E", "F", "E", "E", "G"},           1},
                                                      {DatumArray{"D", "E", "E", "E", "F", "E", "E", "E", "E", "G"}, 1},
                                                      {DatumArray{"D", "E", "E", "E", "E", "F", "E", "E", "E", "F", "E",
                                                                  "E", "E", "G"},                                    1},
                                                      {DatumArray{"D", "E", "E", "F", "E", "E", "F", "E", "E", "F", "E",
                                                                  "E",
                                                                  "G"},                                              1}};
    std::vector<std::pair<int128_t, int64_t>> expected = {{0,  1},
                                                          {1,  1},
                                                          {2,  1},
                                                          {3,  1},
                                                          {4,  1},
                                                          {5,  1},
                                                          {6,  1},
                                                          {7,  1},
                                                          {8,  1},
                                                          {9,  0},
                                                          {10, 0},
                                                          {11, 0},
                                                          {12, 0},
                                                          {13, 0}};
    Run(pairs1, pairs2, 3, 3, expected);
}

} // namespace starrocks
