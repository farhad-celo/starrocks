#pragma once

#include "column/column_helper.h"
#include "column/object_column.h"
#include "column/type_traits.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"
#include <set>
#include <boost/algorithm/string/join.hpp>

namespace starrocks {

struct CelonisKMeansModelAggregateState {

    void update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        if (inconsistent_dimension_) {
            return;
        }
        if (ctx->is_notnull_constant_column(1)) {
            num_clusters_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(1));
        }
        if (ctx->is_notnull_constant_column(2)) {
            seed_ = ColumnHelper::get_const_value<TYPE_INT>(ctx->get_constant_column(2));
        }
        if (num_clusters_ <= 0) {
            std::string msg = "K (number of clusters) must be positive, however it is " + std::to_string(num_clusters_);
            ctx->set_error(msg.c_str(), false);
            return;
        }
        if (seed_ < 0) {
            std::string msg = "Random seed must be non-negative, however it is " + std::to_string(seed_);
            ctx->set_error(msg.c_str(), false);
            return;
        }
        if (columns[0]->is_nullable() && columns[0]->is_null(row_num)) {
            return;
        }
        auto array = columns[0]->get(row_num).get_array();
        if (num_features_ <= 0) {
            num_features_ = array.size();
        } else {
            if (num_features_ != array.size()) {
                inconsistent_dimension_ = true;
                return;
            }
        }
        std::vector<double> point;
        for (auto i = 0; i < array.size(); ++i) {
            if (array[i].is_null()) {
                // ignore point with NULL value(s).
                return;
            } else {
                point.push_back(array[i].get<double>());
            }
        }
        ++num_points_;
        points_.push_back(point);
    }

    // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(uint8_t);                    // inconsistent_dimension_
        result += sizeof(int64_t);                    // num_clusters_
        result += sizeof(int);                        // seed_
        result += sizeof(size_t);                     // num_features_
        result += sizeof(size_t);                     // num_points_
        size_t num_values = num_points_ * num_features_;
        result += sizeof(double) * num_values;        // items in points_
        return result;
    }

    // Writes and binary encoded version of the object to dst.
    void serialize(uint8_t* dst) const {
        memcpy(dst, &inconsistent_dimension_, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        memcpy(dst, &num_clusters_, sizeof(int64_t));
        dst += sizeof(double);
        memcpy(dst, &seed_, sizeof(int));
        dst += sizeof(int);
        memcpy(dst, &num_features_, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &num_points_, sizeof(size_t));
        dst += sizeof(size_t);
        for (const auto& point: points_) {
            for (auto num: point) {
                memcpy(dst, &num, sizeof(double));
                dst += sizeof(double);
            }
        }
    }

    // Deserializes a CelonisKMeansModelAggregateState object and merges it with the current state.
    void deserialize_and_merge(const uint8_t* src, size_t len) {
        const uint8_t* end = src + len;
        bool inconsistent_dimension = false;
        memcpy(&inconsistent_dimension, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        if (inconsistent_dimension) {
            inconsistent_dimension_ = true;
            return;
        }
        memcpy(&num_clusters_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        memcpy(&seed_, src, sizeof(int));
        src += sizeof(int);
        size_t num_features;
        memcpy(&num_features, src, sizeof(size_t));
        src += sizeof(size_t);
        if (num_features_ <= 0) {
            num_features_ = num_features;
        } else {
            if (num_features_ != num_features) {
                inconsistent_dimension_ = true;
                return;
            }
        }
        size_t num_points = 0;
        memcpy(&num_points, src, sizeof(size_t));
        src += sizeof(size_t);
        num_points_ += num_points;
        for (auto i = 0; i < num_points; ++i) {
            std::vector<double> point;
            point.reserve(num_features_);
            for (auto j = 0; j < num_features_; ++j) {
                double num = 0.0;
                memcpy(&num, src, sizeof(double));
                src += sizeof(double);
                point.push_back(num);
            }
            points_.push_back(point);
        }
        DCHECK_EQ(src, end);
    }

    bool is_initialized() const {
        return num_clusters_ != -1;
    }

    int64_t num_clusters() const {
        return num_clusters_;
    }

    size_t num_features() const {
        return num_features_;
    }

    bool inconsistent_dimension() const {
        return inconsistent_dimension_;
    }

    int random_seed() const {
        return seed_;
    }

    const std::vector<std::vector<double>>& points() const {
        return points_;
    }

private:
    bool inconsistent_dimension_ = false;
    int64_t num_clusters_ = -1;
    int seed_ = 0;
    size_t num_points_ = 0;
    size_t num_features_ = 0;
    std::vector<std::vector<double>> points_;
};

/**
 * @param: [point_column, NUM_CLUSTERS, RANDOM_SEED]
 * @paramType: [ARRAY_DOUBLE, CONST BIGINT, CONST INT]
 * @return: VARCHAR
 * point_column: Each row represents a point. All points must have the same number of dimensions.
 * NUM_CLUSTERS: Number of clusters (must be constant).
 * RANDOM_SEED: Random seed used to make the implementation deterministic (must be constant).
 * This function (CELONIS_BUILD_KMEANS_MODEL) implements k-means++ algorithm to train a model (finding the centroids of
 * the clusters). It returns the model encoded in a string in the below format:
 * "min_1,max_1;...;min_m,max_m:x_11,x_12,...,x_1m;x_21,x_22,...,x_2m;...;x_k1,x_k2,...,x_km"
 * If a point is NULL or contains a NULL value, it is ignored in the training.
 * If NUM_CLUSTERS > # of valid points, we set num of clusters to min(NUM_CLUSTERS, # of valid points).
 */
class CelonisKMeansAggregationFunction final
        : public AggregateFunctionBatchHelper<CelonisKMeansModelAggregateState,
                CelonisKMeansAggregationFunction> {
public:

    void
    update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state, size_t row_num) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    void serialize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                             Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    void finalize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                            Column* to) const override;

    std::string get_name() const override;
};

} // namespace starrocks
