#include <gtest/gtest.h>

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "../util.h"
#include "column/struct_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

namespace {

static double ABS_ERROR = 1e-6;

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

class CelonisBuildKMeansModelTest : public testing::Test {
protected:
    CelonisBuildKMeansModelTest() = default;

    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type() {
        TypeDescriptor type_return_varchar;
        type_return_varchar.type = LogicalType::TYPE_VARCHAR;
        return type_return_varchar;
    }

    std::unique_ptr<FunctionContext> get_ctx() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                CelonisAnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_DOUBLE)), // point_column
                CelonisAnyValUtil::column_type_to_type_desc(
                        TypeDescriptor::from_logical_type(TYPE_BIGINT)), // NUM_CLUSTERS
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)), // RANDOM_SEED
        };
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
        mem_pools_.emplace_back(std::make_unique<MemPool>());
        runtime_states_.emplace_back(std::make_unique<RuntimeState>());
        return std::unique_ptr<FunctionContext>(FunctionContext::create_context(
                runtime_states_.back().get(), mem_pools_.back().get(), return_type, std::move(arg_types)));
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*> RunUpdate(
            const DatumArray& points, int64_t num_clusters, int random_seed) {
        auto local_ctx = get_ctx();

        const AggregateFunction* func =
                get_aggregate_function("celonis_build_kmeans_model", TYPE_ARRAY, TYPE_VARCHAR, false);

        DCHECK(func != nullptr);

        auto point_col = ColumnHelper::create_column(celonis::array_type(TYPE_DOUBLE), true);
        for (const auto& point : points) {
            point_col->append_datum(point);
        }
        auto num_clusters_col = ColumnHelper::create_const_column<TYPE_BIGINT>(num_clusters, points.size());
        auto random_seed_col = ColumnHelper::create_const_column<TYPE_INT>(random_seed, points.size());

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = point_col.get();
        raw_columns[1] = num_clusters_col.get();
        raw_columns[2] = random_seed_col.get();
        local_ctx->set_constant_columns({nullptr, num_clusters_col, random_seed_col});

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), points.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    bool parse_model(const std::string& model, std::vector<std::pair<double, double>>& limits,
                     std::vector<std::vector<double>>& centroids) {
        std::vector<std::string> parts;
        boost::split(parts, model, boost::is_any_of(":"));
        if (parts.size() != 2) {
            return false;
        }
        const std::string& limits_str = parts[0];
        const std::string& centroids_str = parts[1];
        std::vector<std::string> limit_rows;
        boost::split(limit_rows, limits_str, boost::is_any_of(";"));
        limits.clear();
        for (size_t row = 0; row < limit_rows.size(); ++row) {
            std::vector<std::string> values;
            boost::split(values, limit_rows[row], boost::is_any_of(","));
            if (values.size() != 2) {
                return false;
            }
            double min_value, max_value;
            try {
                min_value = boost::lexical_cast<double>(values[0]);
                max_value = boost::lexical_cast<double>(values[1]);
            } catch (const boost::bad_lexical_cast& e) {
                return false;
            }
            limits.emplace_back(min_value, max_value);
        }
        const auto nfeatures = limits.size();
        std::vector<std::string> rows;
        boost::split(rows, centroids_str, boost::is_any_of(";"));
        centroids.clear();
        for (size_t row = 0; row < rows.size(); ++row) {
            std::vector<std::string> values;
            boost::split(values, rows[row], boost::is_any_of(","));
            if (!centroids.empty() && values.size() != centroids.back().size()) {
                return false;
            }
            if (values.size() != nfeatures) {
                return false;
            }
            std::vector<double> centroid;
            centroid.reserve(values.size());
            for (size_t col = 0; col < values.size(); ++col) {
                try {
                    auto value = boost::lexical_cast<double>(values[col]);
                    centroid.push_back(value);
                } catch (const boost::bad_lexical_cast& e) {
                    return false;
                }
            }
            centroids.emplace_back(centroid);
        }
        return true;
    }

    void Run(const DatumArray& points, int64_t num_clusters, int random_seed,
             const std::vector<std::pair<double, double>>& expected_limits,
             const std::vector<std::vector<double>>& expected_centroids, bool is_null = false) {
        auto [local_ctx, state, func] = RunUpdate(points, num_clusters, random_seed);

        auto result = ColumnHelper::create_column(get_return_type(), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());
        ASSERT_EQ(1, result->size());
        if (is_null) {
            EXPECT_TRUE(result->get(0).is_null());
        } else {
            const std::string model = result->get(0).get_slice().to_string();
            match_model(model, expected_limits, expected_centroids);
        }
    }

    void match_model(const std::string& model, const std::vector<std::pair<double, double>>& expected_limits,
                     const std::vector<std::vector<double>>& expected_centroids) {
        std::vector<std::vector<double>> centroids;
        std::vector<std::pair<double, double>> limits;
        ASSERT_TRUE(parse_model(model, limits, centroids));
        const auto nfeatures = expected_limits.size();
        EXPECT_EQ(nfeatures, limits.size());
        const auto nrows = expected_centroids.size();
        EXPECT_EQ(nrows, centroids.size());
        for (auto i = 0; i < nfeatures; ++i) {
            EXPECT_NEAR(limits[i].first, expected_limits[i].first, ABS_ERROR);
            EXPECT_NEAR(limits[i].second, expected_limits[i].second, ABS_ERROR);
        }
        if (expected_centroids.empty()) {
            return;
        }
        const auto ncols = expected_centroids[0].size();
        for (auto row = 0; row < nrows; ++row) {
            EXPECT_EQ(ncols, centroids[row].size());
            for (auto col = 0; col < ncols; ++col) {
                EXPECT_NEAR(expected_centroids[row][col], centroids[row][col], ABS_ERROR);
            }
        }
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
    std::vector<std::unique_ptr<RuntimeState>> runtime_states_;
};

// TODO(y.zhang): Add more tests.
TEST_F(CelonisBuildKMeansModelTest, one_feature_large_k) {
    auto points1 = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    auto points2 = DatumArray{DatumArray{3.0}, DatumArray{4.0}};
    int64_t num_clusters = 5; // num_clusters > # of points
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    ASSERT_TRUE(local_ctx1->has_error());
    const char* error = local_ctx1->error_msg();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(std::string_view(error),
              "CELONIS_BUILD_KMEANS_MODEL: not enough rows 4 provided for training of size k 5");
}

TEST_F(CelonisBuildKMeansModelTest, cancellation_work) {
    auto points1 = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    auto points2 = DatumArray{DatumArray{3.0}, DatumArray{4.0}};
    int64_t num_clusters = 2;
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    // set is_cancelled to true
    local_ctx1->state()->set_is_cancelled(true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    ASSERT_TRUE(local_ctx1->has_error());
}

TEST_F(CelonisBuildKMeansModelTest, one_feature_small_k) {
    auto points1 = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    auto points2 = DatumArray{DatumArray{3.0}, DatumArray{4.0}};
    int64_t num_clusters = 1;
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    std::vector<std::vector<double>> expected_centroids = {{0.5}};
    std::vector<std::pair<double, double>> expected_limits = {{1, 4}};
    match_model(result->get(0).get_slice().to_string(), expected_limits, expected_centroids);
}

TEST_F(CelonisBuildKMeansModelTest, two_features_large_k) {
    auto points1 = DatumArray{DatumArray{0.0, 0.0}, DatumArray{0.0, 1.0}};
    auto points2 = DatumArray{DatumArray{1.0, 0.0}, DatumArray{1.0, 1.0}};
    int64_t num_clusters = 5; // num_clusters > # of points
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    ASSERT_TRUE(local_ctx1->has_error());
    const char* error = local_ctx1->error_msg();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(std::string_view(error),
              "CELONIS_BUILD_KMEANS_MODEL: not enough rows 4 provided for training of size k 5");
}

TEST_F(CelonisBuildKMeansModelTest, two_features_small_k) {
    auto points1 = DatumArray{DatumArray{0.0, 0.0}, DatumArray{0.0, 1.0}};
    auto points2 = DatumArray{DatumArray{1.0, 0.0}, DatumArray{1.0, 1.0}};
    int64_t num_clusters = 1;
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    std::vector<std::vector<double>> expected_centroids = {{0.5, 0.5}};
    std::vector<std::pair<double, double>> expected_limits = {{0.0, 1.0}, {0.0, 1.0}};
    match_model(result->get(0).get_slice().to_string(), expected_limits, expected_centroids);
}

TEST_F(CelonisBuildKMeansModelTest, two_features_null_rows_ignored) {
    auto points1 = DatumArray{DatumArray{0.0, 0.0}, DatumArray{0.0, 1.0}, kNullDatum};
    auto points2 = DatumArray{DatumArray{1.0, 0.0}, kNullDatum, DatumArray{1.0, 1.0}};
    int64_t num_clusters = 2;
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    std::vector<std::vector<double>> expected_centroids = {{0.666667, 0.333333}, {0, 1}};
    std::vector<std::pair<double, double>> expected_limits = {{0, 1}, {0, 1}};
    match_model(result->get(0).get_slice().to_string(), expected_limits, expected_centroids);
}

TEST_F(CelonisBuildKMeansModelTest, two_features_null_values_ignored) {
    auto points1 = DatumArray{DatumArray{0.0, 0.0}, DatumArray{0.0, 1.0}, DatumArray{kNullDatum, 8.0}};
    auto points2 = DatumArray{DatumArray{1.0, 0.0}, DatumArray{-1.0, kNullDatum}, DatumArray{1.0, 1.0}};
    int64_t num_clusters = 1;
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    std::vector<std::vector<double>> expected_centroids = {{0.5, 0.5}};
    std::vector<std::pair<double, double>> expected_limits = {{0, 1}, {0, 1}};
    match_model(result->get(0).get_slice().to_string(), expected_limits, expected_centroids);
}

TEST_F(CelonisBuildKMeansModelTest, negative_num_clusters) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    int64_t num_clusters = -1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, zero_num_clusters) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    int64_t num_clusters = 0;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, negative_random_seed) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    int64_t num_clusters = 1;
    double random_seed = -1;
    Run(points, num_clusters, random_seed, {}, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, no_valid_points) {
    auto points = DatumArray{DatumArray{1.0, kNullDatum}, DatumArray{kNullDatum, 2.0}};
    int64_t num_clusters = 1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, zero_point_dimension) {
    auto points = DatumArray{DatumArray{}, DatumArray{}};
    int64_t num_clusters = 1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, inconsistent_point_dimension) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0, 3.0}};
    int64_t num_clusters = 1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, merge_empty_state) {
    auto points1 = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    auto points2 = DatumArray{kNullDatum}; // Input that results in 0 valid points, but initializes num_clusters_
    int64_t num_clusters = 1;
    double random_seed = 0;

    auto [local_ctx1, state1, func] = RunUpdate(points1, num_clusters, random_seed);
    auto [local_ctx2, state2, func2] = RunUpdate(points2, num_clusters, random_seed);

    // Serialize state2 (empty)
    ColumnPtr serialize_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serialize_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serialize_col.get(), state1->state(), 0);

    // Get the result - should not fail
    auto result = ColumnHelper::create_column(get_return_type(), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    std::vector<std::vector<double>> expected_centroids = {{0.5}};
    std::vector<std::pair<double, double>> expected_limits = {{1, 2}};
    match_model(result->get(0).get_slice().to_string(), expected_limits, expected_centroids);
}

TEST_F(CelonisBuildKMeansModelTest, single_cluster_optimization_multi_dimensional) {
    // Create a larger dataset with 3 features to test the compute_mean optimization
    auto points = DatumArray{
            DatumArray{1.0, 2.0, 3.0},    DatumArray{4.0, 5.0, 6.0},    DatumArray{7.0, 8.0, 9.0},
            DatumArray{10.0, 11.0, 12.0}, DatumArray{13.0, 14.0, 15.0}, DatumArray{16.0, 17.0, 18.0},
            DatumArray{19.0, 20.0, 21.0}, DatumArray{22.0, 23.0, 24.0},
    };
    int64_t num_clusters = 1;
    double random_seed = 42;

    // Expected mean for each dimension:
    // Dimension 0: (1+4+7+10+13+16+19+22)/8 = 92/8 = 11.5
    // Dimension 1: (2+5+8+11+14+17+20+23)/8 = 100/8 = 12.5
    // Dimension 2: (3+6+9+12+15+18+21+24)/8 = 108/8 = 13.5
    // After normalization: all become (11.5-1)/(22-1) = 10.5/21 = 0.5 for each dimension
    std::vector<std::vector<double>> expected_centroids = {{0.5, 0.5, 0.5}};
    std::vector<std::pair<double, double>> expected_limits = {{1.0, 22.0}, {2.0, 23.0}, {3.0, 24.0}};

    Run(points, num_clusters, random_seed, expected_limits, expected_centroids);
}

} // namespace starrocks
