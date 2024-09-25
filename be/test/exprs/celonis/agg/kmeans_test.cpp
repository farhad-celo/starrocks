#include <algorithm>
#include <gtest/gtest.h>

#include "../util.h"
#include "column/struct_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "exprs/celonis/util.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

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
                AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_DOUBLE)),                 // point_column
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),   // NUM_CLUSTERS
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),      // RANDOM_SEED
        };
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
        mem_pools_.emplace_back(std::make_unique<MemPool>());
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_context(nullptr, mem_pools_.back().get(), return_type, std::move(arg_types)));
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(const DatumArray& points, int64_t num_clusters, int random_seed) {
        auto local_ctx = get_ctx();

        const AggregateFunction* func =
                get_aggregate_function("celonis_build_kmeans_model", TYPE_ARRAY, TYPE_VARCHAR, false);

        DCHECK(func != nullptr);

        auto point_col = ColumnHelper::create_column(celonis::array_type(TYPE_DOUBLE), true);
        for (const auto& point: points) {
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

    bool parse_model(const std::string& model, std::vector<std::vector<double>>& centroids) {
        std::vector<std::string> rows;
        boost::split(rows, model, boost::is_any_of(";"));
        centroids.clear();
        for (size_t row = 0; row < rows.size(); ++row) {
            std::vector<std::string> values;
            boost::split(values, rows[row], boost::is_any_of(","));
            if (!centroids.empty() && values.size() != centroids.back().size()) {
                return false;
            }
            if (values.empty()) {
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
             const std::vector<std::vector<double>>& expected_centroids, bool is_null = false) {
        auto [local_ctx, state, func] = RunUpdate(points, num_clusters, random_seed);

        auto result = ColumnHelper::create_column(get_return_type(), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());
        ASSERT_EQ(1, result->size());
        if (is_null) {
            EXPECT_TRUE(result->get(0).is_null());
        } else {
            const std::string model = result->get(0).get_slice().to_string();
            match_model(model, expected_centroids);
        }
    }

    void match_model(const std::string& model, const std::vector<std::vector<double>>& expected_centroids) {
        std::vector<std::vector<double>> centroids;
        ASSERT_TRUE(parse_model(model, centroids));
        const auto nrows = expected_centroids.size();
        EXPECT_EQ(nrows, centroids.size());
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

    std::vector<std::vector<double>> expected_centroids = {{1},
                                                           {2},
                                                           {3},
                                                           {4}};
    match_model(result->get(0).get_slice().to_string(), expected_centroids);
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

    std::vector<std::vector<double>> expected_centroids = {{2.5}};
    match_model(result->get(0).get_slice().to_string(), expected_centroids);
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

    std::vector<std::vector<double>> expected_centroids = {{0.0, 0.0},
                                                           {0.0, 1.0},
                                                           {1.0, 0.0},
                                                           {1.0, 1.0}};
    match_model(result->get(0).get_slice().to_string(), expected_centroids);
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
    match_model(result->get(0).get_slice().to_string(), expected_centroids);
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

    std::vector<std::vector<double>> expected_centroids = {{0.666667, 0.333333},
                                                           {0,        1}};
    match_model(result->get(0).get_slice().to_string(), expected_centroids);
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
    match_model(result->get(0).get_slice().to_string(), expected_centroids);
}

TEST_F(CelonisBuildKMeansModelTest, negative_num_clusters) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    int64_t num_clusters = -1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, zero_num_clusters) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    int64_t num_clusters = 0;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, negative_random_seed) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0}};
    int64_t num_clusters = 1;
    double random_seed = -1;
    Run(points, num_clusters, random_seed, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, no_valid_points) {
    auto points = DatumArray{DatumArray{1.0, kNullDatum}, DatumArray{kNullDatum, 2.0}};
    int64_t num_clusters = 1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, zero_point_dimension) {
    auto points = DatumArray{DatumArray{}, DatumArray{}};
    int64_t num_clusters = 1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, true);
}

TEST_F(CelonisBuildKMeansModelTest, inconsistent_point_dimension) {
    auto points = DatumArray{DatumArray{1.0}, DatumArray{2.0, 3.0}};
    int64_t num_clusters = 1;
    double random_seed = 0;
    Run(points, num_clusters, random_seed, {}, true);
}

} // namespace starrocks

