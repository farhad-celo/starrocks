#include "kmeans.h"

#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <limits>

#include "column/column_helper.h"
#include "exprs/celonis/agg/util.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

namespace {

static const double MAX_KMEANS_SECONDS = 3.5 * 60.0; // 3.5 mins
static const int MAX_KMEANS_ITERATIONS = 100;
static const uint64_t MAX_KMEANS_MODEL_SIZE = (100LL << 20); // 100M

std::vector<std::pair<double, double>> get_limits(const std::vector<std::vector<double>>& points) {
    if (points.empty()) {
        return {};
    }
    const auto npoints = points.size();
    const auto nfeatures = points[0].size();
    std::vector<std::pair<double, double>> limits;
    limits.reserve(nfeatures);
    DCHECK(nfeatures != 0);
    for (auto i = 0; i < nfeatures; ++i) {
        std::vector<double> values;
        values.reserve(npoints);
        for (auto j = 0; j < npoints; ++j) {
            values.push_back(points[j][i]);
        }
        auto result = std::minmax_element(values.begin(), values.end());
        limits.emplace_back(*result.first, *result.second);
    }
    return limits;
}

// Applies min-max scaling normalization. It returns {limits, normalized_points}.
// when min == max, set all values of the feature to 0.
std::pair<std::vector<std::pair<double, double>>, std::vector<std::vector<double>>>
normalize_points(const std::vector<std::vector<double>>& points) {
    if (points.empty()) {
        return {};
    }
    const auto npoints = points.size();
    const auto nfeatures = points[0].size();
    const auto limits = get_limits(points);
    const double epsilon = 1e-9;
    std::vector<std::vector<double>> rv = points;
    for (auto i = 0; i < nfeatures; ++i) {
        auto [min_value, max_value] = limits[i];
        if (max_value - min_value < epsilon) {
            // set value to zero
            for (auto j = 0; j < npoints; ++j) {
                rv[j][i] = 0.0;
            }
        } else {
            double value_range = max_value - min_value;
            for (auto j = 0; j < npoints; ++j) {
                rv[j][i] = (rv[j][i] - min_value) / value_range;
            }
        }
    }
    return {limits, rv};
}

std::string to_string(double value) {
    std::string decimal_str = std::to_string(value);
    size_t decimal_point = decimal_str.find('.');

    if (decimal_point == std::string::npos) {
        return decimal_str;
    }

    size_t last_non_zero = decimal_str.find_last_not_of('0');

    // If the last non-zero character is the decimal point itself, remove it as well
    if (last_non_zero == decimal_point) {
        return decimal_str.substr(0, decimal_point);
    }

    // Otherwise, return the string up to the last non-zero character
    return decimal_str.substr(0, last_non_zero + 1);
}

std::optional<std::string>
to_model(const std::vector<std::pair<double, double>>& limits, const std::vector<std::vector<double>>& centroids) {
    uint64_t size = 0;
    std::vector<std::string> limit_strs;
    limit_strs.reserve(limits.size());
    for (const auto& [min_value, max_value]: limits) {
        std::string limit_str = to_string(min_value) + "," + to_string(max_value);
        size += limit_str.size() + 1;
        if (size > MAX_KMEANS_MODEL_SIZE) {
            return std::nullopt;
        }
        limit_strs.push_back(limit_str);
    }
    std::vector<std::string> row_strs;
    row_strs.reserve(centroids.size());
    for (const auto& centroid: centroids) {
        std::vector<std::string> value_strs;
        value_strs.reserve(centroid.size());
        for (double value: centroid) {
            value_strs.push_back(to_string(value));
        }
        const auto row_str = boost::algorithm::join(value_strs, ",");
        size += row_str.size() + 1;
        if (size > MAX_KMEANS_MODEL_SIZE) {
            return std::nullopt;
        }
        row_strs.push_back(row_str);
    }
    return boost::algorithm::join(limit_strs, ";") + ":" + boost::algorithm::join(row_strs, ";");
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

    void run() {
        auto start_time = std::chrono::high_resolution_clock::now();
        initialize_centroids();

        for (int iter = 0; iter < MAX_KMEANS_ITERATIONS; ++iter) {
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
    LOG(INFO) << "CELONIS_BUILD_KMEANS_MODEL: # of points is " << points.size() << std::endl;

    if (points.empty() || state_impl.inconsistent_dimension() || state_impl.num_features() == 0) {
        to->append_default();
        return;
    }
    if (UNLIKELY(ctx->state()->cancelled_ref())) {
        ctx->set_error("celonis_build_kmeans_model detects cancelled.", false);
        to->append_default();
        return;
    }
    DCHECK_GT(num_clusters, 0);
    std::vector<std::vector<double>> centroids;
    const auto& [limits, normalized_points] = normalize_points(points);
    if (num_clusters >= normalized_points.size()) {
        centroids = normalized_points;
        std::sort(centroids.begin(), centroids.end());
    } else {
        LOG(INFO) << "CELONIS_BUILD_KMEANS_MODEL: started k-means clustering.\n";
        KMeansPlusPlus kmeans(normalized_points, num_clusters, static_cast<unsigned int>(random_seed));
        kmeans.run();
        LOG(INFO) << "CELONIS_BUILD_KMEANS_MODEL: done k-means clustering.\n";
        centroids = kmeans.get_centroids();
    }
    LOG(INFO) << "CELONIS_BUILD_KMEANS_MODEL: started to_model.\n";
    const auto model = to_model(limits, centroids);
    LOG(INFO) << "CELONIS_BUILD_KMEANS_MODEL: done to_model.\n";
    if (model.has_value()) {
        to->append_datum(model->c_str());
    } else {
        ctx->set_error(
                std::string("CELONIS_BUILD_KMEANS_MODEL: output model string size exceeds the limit (100M)").c_str(),
                false);
        to->append_default();
    }
}

std::string CelonisKMeansAggregationFunction::get_name() const { return "celonis_build_kmeans_model"; }

} // namespace starrocks
