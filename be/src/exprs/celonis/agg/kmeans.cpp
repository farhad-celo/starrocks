#include "kmeans.h"

#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <limits>

#include "column/column_helper.h"
#include "exprs/celonis/agg/util.h"
#include "runtime/mem_pool.h"

namespace starrocks {

namespace {

static const double MAX_KMEANS_SECONDS = 3.5 * 60.0; // 3.5 mins

std::string to_model(const std::vector<std::vector<double>>& centroids) {
    std::vector<std::string> row_strs;
    row_strs.reserve(centroids.size());
    for (const auto& centroid: centroids) {
        std::vector<std::string> value_strs;
        value_strs.reserve(centroid.size());
        for (double value: centroid) {
            value_strs.push_back(std::to_string(value));
        }
        row_strs.push_back(boost::algorithm::join(value_strs, ","));
    }
    return boost::algorithm::join(row_strs, ";");
}

class KMeansPlusPlus {
private:
    std::vector<std::vector<double>> points;
    int64_t k;
    std::vector<std::vector<double>> centroids;
    std::vector<int> assignments;
    std::mt19937 gen;

    double euclidean_distance_squared(const std::vector<double>& a, const std::vector<double>& b) {
        double sum = 0.0;
        auto size = std::min(a.size(), b.size());
        for (size_t i = 0; i < size; ++i) {
            double diff = a[i] - b[i];
            sum += diff * diff;
        }
        return sum;
    }

    void initialize_centroids() {
        // Cap k at the number of points
        k = std::min(k, static_cast<int64_t>(points.size()));
        // Sort points to get deterministic result
        std::sort(points.begin(), points.end());

        // If k equals the number of points, use all points as centroids
        if (k == static_cast<int>(points.size())) {
            centroids = points;
            return;
        }

        // Choose the first centroid randomly
        DCHECK(!points.empty());
        std::uniform_int_distribution<> distrib(0, points.size() - 1); // Define the range [0, size-1]
        auto first_centroid = distrib(gen);
        centroids.push_back(points[first_centroid]);
        std::set<size_t> available_indexes;
        for (size_t i = 0; i < points.size(); ++i) {
            if (i != first_centroid) {
                available_indexes.insert(i);
            }
        }

        // Choose the remaining k - 1 centroids
        for (int i = 1; i < k; ++i) {
            double total_distance_squared = 0.0;

            // Compute distances to the nearest centroid for each non-centroid point
            std::vector<double> distances_squared;
            distances_squared.reserve(available_indexes.size());
            for (size_t j: available_indexes) {
                auto min_distance_squared = std::numeric_limits<double>::max();
                for (const auto& centroid: centroids) {
                    double dist_squared = euclidean_distance_squared(points[j], centroid);
                    min_distance_squared = std::min(min_distance_squared, dist_squared);
                }
                total_distance_squared += min_distance_squared;
                distances_squared.push_back(min_distance_squared);
            }

            // Choose the next centroid with probability proportional to distance squared
            std::uniform_real_distribution<> dis_real(0.0, total_distance_squared);
            double r = dis_real(gen);
            double sum = 0.0;
            size_t idx = 0;
            for (size_t j: available_indexes) {
                sum += distances_squared[idx++];
                if (sum >= r) {
                    centroids.push_back(points[j]);
                    available_indexes.erase(j);
                    break;
                }
            }
        }
    }

    void assign_points_to_clusters() {
        assignments.resize(points.size());
        for (size_t i = 0; i < points.size(); ++i) {
            double min_dist_squared = std::numeric_limits<double>::max();
            int closest_centroid = 0;
            for (int j = 0; j < k; ++j) {
                double dist_squared = euclidean_distance_squared(points[i], centroids[j]);
                if (dist_squared < min_dist_squared) {
                    min_dist_squared = dist_squared;
                    closest_centroid = j;
                }
            }
            assignments[i] = closest_centroid;
        }
    }

    void update_centroids() {
        std::vector<std::vector<double>> new_centroids(k, std::vector<double>(points[0].size(), 0.0));
        std::vector<int> cluster_sizes(k, 0);

        for (size_t i = 0; i < points.size(); ++i) {
            int cluster = assignments[i];
            cluster_sizes[cluster]++;
            for (size_t j = 0; j < points[i].size(); ++j) {
                new_centroids[cluster][j] += points[i][j];
            }
        }

        for (int i = 0; i < k; ++i) {
            if (cluster_sizes[i] > 0) {
                for (size_t j = 0; j < new_centroids[i].size(); ++j) {
                    new_centroids[i][j] /= cluster_sizes[i];
                }
            } else {
                // If a cluster is empty, keep its old centroid
                new_centroids[i] = centroids[i];
            }
        }

        centroids = new_centroids;
    }

public:
    KMeansPlusPlus(const std::vector<std::vector<double>>& input_points, int64_t num_clusters, unsigned int seed)
            : points(input_points), k(num_clusters), gen(seed) {}

    void run(int max_iterations = 100) {
        auto start_time = std::chrono::high_resolution_clock::now();
        initialize_centroids();

        for (int iter = 0; iter < max_iterations; ++iter) {
            assign_points_to_clusters();
            update_centroids();
            auto cur_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = cur_time - start_time;
            if (elapsed_time.count() > MAX_KMEANS_SECONDS) {
                break;
            }
        }
    }

    const std::vector<std::vector<double>>& get_centroids() const {
        return centroids;
    }

    int get_actual_k() const {
        return k;
    }
};

} // namespace

void CelonisKMeansAggregationFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr state,
                                              size_t row_num) const {
    auto& state_impl = this->data(state);
    state_impl.update(ctx, columns, row_num);
}

void CelonisKMeansAggregationFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                             size_t row_num) const {
    // merge internal state with column[row_num]
    // the column type is binary
    const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
    if (input_column->is_null(row_num)) {
        return;
    }
    Slice slice = input_column->get_slice(row_num);
    this->data(state).deserialize_and_merge((const uint8_t*) slice.data, slice.size);
}

void CelonisKMeansAggregationFunction::serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                           Column* to) const {
    // append our serialized state to column "to"
    auto* column = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(to));
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
    }
    size_t old_size = column->get_bytes().size();
    size_t new_size = old_size + this->data(state).serialized_size();
    column->get_bytes().resize(new_size);
    this->data(state).serialize(column->get_bytes().data() + old_size);
    column->get_offset().emplace_back(new_size);
}

void CelonisKMeansAggregationFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                   size_t chunk_size,
                                                                   ColumnPtr* dst) const {
    // Used for streaming aggregation passthrough. Not implemented.
    throw std::runtime_error("celonis_build_kmeans_model: convert_to_serialize_format not supported");
}

void CelonisKMeansAggregationFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                          Column* to) const {
    auto& state_impl = this->data(state);
    if (!state_impl.is_initialized()) {
        to->append_default();
        return;
    }
    const auto& points = state_impl.points();
    int random_seed = state_impl.random_seed();
    int64_t num_clusters = state_impl.num_clusters();

    if (points.empty()) {
        to->append_default();
        return;
    }

    DCHECK_GT(num_clusters, 0);
    std::vector<std::vector<double>> centroids;
    if (num_clusters >= points.size()) {
        centroids = points;
        std::sort(centroids.begin(), centroids.end());
    } else {
        KMeansPlusPlus kmeans(points, num_clusters, static_cast<unsigned int>(random_seed));
        kmeans.run();
        centroids = kmeans.get_centroids();
    }
    std::string model = to_model(centroids);
    to->append_datum(model.c_str());
}

std::string CelonisKMeansAggregationFunction::get_name() const { return "celonis_build_kmeans_model"; }

} // namespace starrocks
