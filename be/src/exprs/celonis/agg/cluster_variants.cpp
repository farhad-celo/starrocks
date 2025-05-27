#include "cluster_variants.h"

#include <queue>
#include <chrono>
#include <tbb/parallel_for.h>

#include "column/column_helper.h"
#include "exprs/celonis/agg/util.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

namespace {

static const int64_t NULL_VARIANT_LABEL = -2;
static const size_t MAX_DISTINCT_VARIANTS = 10000000;
static const double MAX_DBSCAN_SECONDS = 10 * 60.0;

struct VariantHashesWithCount {
    std::vector<int128_t> hashes;
    int64_t count = 0;
};

struct Clusterer {
    int64_t min_pts;
    int64_t epsilon;
    // prefix index
    // An inverted index on the prefix tokens is used to retrieve candidate pairs efficiently. The inverted index maps
    // prefix tokens to sets that contain that token in the prefix. A lookup of set s retrieves all lists of the prefix
    // tokens of s. The union of these lists (except s itself) are the candidates of s.
    phmap::flat_hash_map<Edge, std::vector<size_t>, HashOnEdge, EqualOnEdge> edge_to_indexes;
    // two levels of bitmasks
    // Each bitmask is a 64 bits integer, each bit represents the presence or absence of an edge.
    // To optimize the computation of symmetric differences between sets, we first calculate the XOR of their
    // corresponding bitmasks. If the XOR value exceeds epsilon, we can immediately conclude that these sets cannot
    // be neighbors, avoiding unnecessary further calculations.
    std::vector<uint64_t> prefix_bitmasks;
    std::vector<uint64_t> second_prefix_bitmasks;
    // true means the (prefix_bitmask + second_prefix_bitmask) is the exact bitmask.
    std::vector<bool> is_bitmask_exacts;
    // the count of is_neighbor method calls.
    int64_t n_is_neighbor_checks = 0;
    // the count of is_neighbor checks using bitmasks.
    int64_t n_shortcut_checks = 0;

    Clusterer(int64_t min_pts, int64_t epsilon) : min_pts(min_pts), epsilon(epsilon) {}

    void build_prefix_bitmasks(const std::vector<EdgeSet>& points,
                               const phmap::flat_hash_map<Edge, int64_t, HashOnEdge, EqualOnEdge>& edge_counter) {
        const auto n_points = points.size();
        prefix_bitmasks.resize(n_points, 0);
        second_prefix_bitmasks.resize(n_points, 0);
        is_bitmask_exacts.resize(n_points, false);
        // Suppose there are n unique edges in all edge sets, we pick the top min(128, n) frequent edges.
        // prefix_bitmask represents the first min(64, n) edges.
        // second_prefix_bitmask represents the next min(64, n - 64) edges.
        // compute the top (most) 2 * 64 frequent edges
        std::vector<std::pair<Edge, int64_t>> temp_edges(std::min(2 * sizeof(uint64_t) * CHAR_BIT, edge_counter.size()));
        std::vector<std::pair<Edge, int64_t>> edge_cnts(edge_counter.begin(), edge_counter.end());
        std::partial_sort_copy(edge_cnts.begin(), edge_cnts.end(), temp_edges.begin(), temp_edges.end(),
                               [](const auto& a, const auto& b) { return a.second > b.second; });
        std::vector<Edge> freq_edges;
        for (const auto& entry: temp_edges) {
            freq_edges.push_back(entry.first);
        }
        // compute the bitmasks
        for (auto i = 0; i < points.size(); ++i) {
            const auto& point = points[i];
            phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge> cur_edges(point.edges.begin(), point.edges.end());
            uint64_t bitmask = 0;
            for (auto j = 0; j < std::min(sizeof(uint64_t) * CHAR_BIT, freq_edges.size()); ++j) {
                if (cur_edges.contains(freq_edges[j])) {
                    bitmask |= (1ULL << j);
                }
            }
            prefix_bitmasks[i] = bitmask;
            uint64_t second_bitmask = 0;
            for (auto j = 64; j < std::min(2 * sizeof(uint64_t) * CHAR_BIT, freq_edges.size()); ++j) {
                if (cur_edges.contains(freq_edges[j])) {
                    second_bitmask |= (1ULL << (j - 64));
                }
            }
            second_prefix_bitmasks[i] = second_bitmask;
            // If all the edges in the edge set are covered by prefix_bitmask and second_prefix_bitmask,
            // is_bitmask_exact = true.
            is_bitmask_exacts[i] = ((__builtin_popcountll(bitmask) + __builtin_popcountll(second_bitmask)) ==
                                    cur_edges.size());
        }
    }

    // Executes DBSCAN and returns cluster labels.
    // points (i.e., edge_sets) are sorted based on length (from high to low).
    // Each point (i.e., edge_set) is sorted based on edge frequency (from low frequency to high frequency).
    std::optional<std::vector<int64_t>> dbscan(const std::vector<EdgeSet>& points, const std::vector<int64_t>& counts,
                                               const phmap::flat_hash_map<Edge, int64_t, HashOnEdge, EqualOnEdge>& edge_counter) {
        auto start_time = std::chrono::high_resolution_clock::now();
        LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: number of unique edges is " << edge_counter.size() << std::endl;
        DCHECK_EQ(points.size(), counts.size());
        n_is_neighbor_checks = 0;
        n_shortcut_checks = 0;
        build_prefix_index(points);
        build_prefix_bitmasks(points, edge_counter);
        LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: size of edge_to_indexes is " << edge_to_indexes.size() << std::endl;
        const auto n_points = points.size();
        std::vector<int64_t> labels(n_points, -1);
        std::vector<bool> is_cores(n_points, false);
        std::vector<bool> is_isolated(n_points, false);
        std::vector<std::vector<size_t>> precomputed_neighbors(n_points);

        // TODO(y.zhang): Parallelizing neighbors computation using TBB is a short term fix for performance issue.
        // This may increase the total CPU time when the number of clusters is small.
        tbb::parallel_for(
                tbb::blocked_range<size_t>(0, n_points),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t i = range.begin(); i != range.end(); ++i) {
                        if (points[i].is_empty_variant) {
                            labels[i] = NULL_VARIANT_LABEL;
                        } else {
                            precomputed_neighbors[i] = get_neighbors(points, i, is_cores, is_isolated);
                        }
                    }
                },
                tbb::auto_partitioner()  // Let TBB decide the best partitioning
        );


        int64_t cluster_id = 0;
        // Traverse the points
        for (auto index = 0; index < points.size(); ++index) {
            if (labels[index] != -1) {
                continue;
            }
            const auto& neighbors = precomputed_neighbors[index];
            if (neighbors.size() == 1) {
                is_isolated[index] = true;
            }
            auto density = compute_density(counts, neighbors);
            if (density < min_pts) {
                // noise
                labels[index] = -1;
                continue;
            }
            expand_cluster(points, counts, index, neighbors, cluster_id, labels, is_cores, is_isolated);
            ++cluster_id;
            if (index % 100 == 0) {
                auto cur_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed_time = cur_time - start_time;
                if (elapsed_time.count() > MAX_DBSCAN_SECONDS) {
                    return std::nullopt;
                }
            }
        }
        LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: number of clusters is " << cluster_id << std::endl;
        LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: n_is_neighbor_checks = " << n_is_neighbor_checks << std::endl;
        LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: n_shortcut_checks = " << n_shortcut_checks << std::endl;
        return labels;
    }

    void build_prefix_index(const std::vector<EdgeSet>& points) {
        edge_to_indexes.clear();
        // Set n_tokens = epsilon + 1, if two sets s1 and s2 do not have overlap in the first n_tokens, then their
        // symmetric difference is at least (epsilon + 1) > epsilon.
        const auto n_tokens = epsilon + 1;
        for (auto index = 0; index < points.size(); ++index) {
            // check the first n_tokens;
            const auto& point = points[index];
            for (auto j = 0; j < n_tokens && j < point.size(); ++j) {
                edge_to_indexes[point.edges[j]].push_back(index);
            }
        }
    }

    int64_t compute_density(const std::vector<int64_t>& counts, const std::vector<size_t>& neighbors) const {
        int64_t rv = 0;
        for (auto index: neighbors) {
            rv += counts[index];
        }
        return rv;
    }

    bool is_neighbor(const std::vector<EdgeSet>& points, size_t i, size_t j) {
        ++n_is_neighbor_checks;
        // Check bitmask first.
        const auto xor_result = prefix_bitmasks[i] ^ prefix_bitmasks[j];
        const auto second_xor_result = second_prefix_bitmasks[i] ^ second_prefix_bitmasks[j];
        // prefix_distance <= symmetric difference
        const auto prefix_distance = __builtin_popcountll(xor_result) + __builtin_popcountll(second_xor_result);
        if ((prefix_distance > epsilon) || (is_bitmask_exacts[i] && is_bitmask_exacts[j])) {
            ++n_shortcut_checks;
            return prefix_distance <= epsilon;
        }
        // Compute the symmetric difference.
        const auto& a = points[i];
        const auto& b = points[j];
        phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge> unique_edges(a.edges.begin(), a.edges.end());
        int64_t distance = 0;
        for (const auto& edge: b.edges) {
            if (unique_edges.contains(edge)) {
                unique_edges.erase(edge);
            } else {
                if (++distance > epsilon) {
                    return false;
                }
            }
        }
        return (distance + unique_edges.size()) <= epsilon;
    }

    // Finds the first index such that points[index].edges.size() <= max_length.
    int find_index(const std::vector<EdgeSet>& points, size_t max_length) const {
        if (points.empty()) {
            return -1;
        }
        if (points.back().edges.size() > max_length) {
            return -1;
        }
        size_t lo = 0;
        size_t hi = points.size() - 1;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (points[mid].edges.size() <= max_length) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    // Finds the first index such that points[indexes[index]].edges.size() <= max_length.
    int find_index(const std::vector<EdgeSet>& points, const std::vector<size_t>& indexes, size_t max_length) const {
        if (indexes.empty()) {
            return -1;
        }
        if (points[indexes.back()].edges.size() > max_length) {
            return -1;
        }
        size_t lo = 0;
        size_t hi = indexes.size() - 1;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (points[indexes[mid]].edges.size() <= max_length) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    std::vector<size_t>
    get_neighbors(const std::vector<EdgeSet>& points, size_t index, const std::vector<bool>& is_cores,
                  const std::vector<bool>& is_isolated) {
        const auto& point = points[index];
        const auto length = point.size();
        // Given two sets s1 and s2, if symmetric_difference(s1, s2) >= abs(len(s1) - len(s2)).
        const auto max_length = length + epsilon;
        const auto min_length = length >= epsilon ? (length - epsilon) : 0;
        HashSet<size_t> candidates = {};
        if (epsilon >= length) {
            size_t start_index = find_index(points, max_length);
            if (start_index != -1) {
                for (auto i = start_index; i < points.size() && points[i].size() >= min_length; ++i) {
                    if (is_isolated[i] || is_cores[i] || index == i) {
                        continue;
                    }
                    candidates.insert(i);
                }
            }
        } else {
            for (auto i = 0; i < epsilon + 1 && i < point.size(); ++i) {
                const Edge& edge = point.edges[i];
                auto it = edge_to_indexes.find(edge);
                if (it == edge_to_indexes.end()) {
                    continue;
                }
                const auto& indexes = it->second;
                size_t start_index = find_index(points, indexes, max_length);
                if (start_index != -1) {
                    for (auto j = start_index; j < indexes.size() && points[indexes[j]].size() >= min_length; ++j) {
                        const auto k = indexes[j];
                        if (is_isolated[k] || is_cores[k] || index == k) {
                            continue;
                        }
                        candidates.insert(indexes[j]);
                    }
                }
            }
        }
        std::vector<size_t> rv = {index};
        for (auto candidate: candidates) {
            if (is_neighbor(points, index, candidate)) {
                rv.push_back(candidate);
            }
        }
        return rv;
    }

    void expand_cluster(const std::vector<EdgeSet>& points, const std::vector<int64_t>& counts,
                        size_t start_index, const std::vector<size_t>& neighbors, int64_t cluster_id,
                        std::vector<int64_t>& labels, std::vector<bool>& is_cores, std::vector<bool>& is_isolated) {
        std::vector<size_t> core_indexes = {start_index};
        labels[start_index] = cluster_id;
        std::deque<size_t> unvisited(neighbors.begin(), neighbors.end());
        while (!unvisited.empty()) {
            size_t neighbor_index = unvisited.front();
            unvisited.pop_front();
            if (labels[neighbor_index] == -1) {
                labels[neighbor_index] = cluster_id;
            } else {
                continue;
            }
            const auto cur_neighbors = get_neighbors(points, neighbor_index, is_cores, is_isolated);
            if (cur_neighbors.size() == 1) {
                is_isolated[neighbor_index] = true;
            }
            if (compute_density(counts, cur_neighbors) >= min_pts) {
                core_indexes.push_back(neighbor_index);
                for (auto cur_neighbor: cur_neighbors) {
                    unvisited.push_back(cur_neighbor);
                }
            }
        }
        for (auto index: core_indexes) {
            is_cores[index] = true;
        }
    }

};

} // namespace


std::pair<int32_t, size_t> ClusterVariantsState::maybe_add_activity(MemPool* mem_pool, const Slice& slice,
                                                                    size_t* memory) {
    int32_t index = 0;
    SliceWithHash key(slice);
    size_t hash = key.hash;

    DCHECK(mem_pool != nullptr);
    auto it = activity_map_.find(key, key.hash);
    if (it == activity_map_.end()) {
        // New activity - allocate memory
        char* pos = (char*) mem_pool->allocate(key.size);
        DCHECK(pos != nullptr);
        memcpy(pos, key.data, key.size);
        *memory += phmap::item_serialize_size<SliceHashMap>::value;
        key.data = pos;
        index = activity_map_.size();
        activity_map_.insert(std::pair<SliceWithHash, int32_t>(key, activity_map_.size()));
    } else {
        key.data = it->first.data;
        index = it->second;
    }
    return std::make_pair(index, hash);
}

void ClusterVariantsAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr state,
                                              size_t row_num) const {
    this->data(state).update(ctx, columns, row_num);
}

void ClusterVariantsAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                             size_t row_num) const {
    // merge internal state with column[row_num]
    if (column->is_null(row_num)) {
        return;
    }
    // the column type is binary
    const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
    Slice slice = input_column->get_slice(row_num);
    size_t mem_usage = 0;
    mem_usage += this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*) slice.data, slice.size);
    ctx->add_mem_usage(mem_usage);
}

void ClusterVariantsAggregateFunction::serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
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

void ClusterVariantsAggregateFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                   size_t chunk_size,
                                                                   ColumnPtr* dst) const {
    // Used for streaming aggregation. Not implemented.
    throw std::runtime_error("celonis_cluster_variants: convert_to_serialize_format not supported");
}

void ClusterVariantsAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                          Column* to) const {
    if (UNLIKELY(!ColumnHelper::get_data_column(to)->is_struct())) {
        ctx->set_error(std::string("The output column of " + get_name() +
                                   " finalize_to_column() is not struct, but is " + to->get_name())
                               .c_str(),
                       false);
        return;
    }
    if (UNLIKELY(ctx->state()->cancelled_ref())) {
        ctx->set_error("cluster_variants detects cancelled.", false);
        return;
    }
    auto& state_impl = this->data(state);
    const auto min_pts = state_impl.min_pts();
    const auto epsilon = state_impl.epsilon();
    const auto& activity_map = state_impl.activity_map();
    const auto& edge_set_map = state_impl.edge_set_map();
    if (edge_set_map.size() > MAX_DISTINCT_VARIANTS) {
        ctx->set_error(std::string(
                "CELONIS_CLUSTER_VARIANTS is currently limited to 10,000,000 distinct variants, however there are " +
                std::to_string(edge_set_map.size()) + " unique variants").c_str(), false);
        return;
    }
    LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: number of unique activities is " << activity_map.size() << std::endl;
    const auto& null_variant_hashes = state_impl.null_variant_hashes();
    // We need to create a map from EdgeSet to (count, vector of variant hashes), then cluster based on it.
    phmap::flat_hash_map<EdgeSet, VariantHashesWithCount, HashOnEdgeSet, EqualOnEdgeSet> edges_map;
    for (const auto& [hash128, edge_set_count]: edge_set_map) {
        auto& hashes_with_count = edges_map[edge_set_count.first];
        hashes_with_count.count += edge_set_count.second;
        hashes_with_count.hashes.emplace_back(hash128);
    }
    LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: number of unique edge sets is " << edge_set_map.size() << std::endl;
    // Compute the frequency of each edge.
    const auto n_points = edges_map.size();
    phmap::flat_hash_map<Edge, int64_t, HashOnEdge, EqualOnEdge> edge_counter;
    std::vector<std::pair<EdgeSet, VariantHashesWithCount>> pairs;
    int64_t total_set_size = 0;
    size_t max_set_size = 0;
    for (auto& entry: edges_map) {
        const EdgeSet& edge_set = entry.first;
        total_set_size += edge_set.size();
        max_set_size = std::max(max_set_size, edge_set.size());
        for (const auto& edge: edge_set.edges) {
            edge_counter[edge] += 1;
        }
        pairs.emplace_back(std::move(entry.first), std::move(entry.second));
    }
    LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: number of unique set representation is " << edges_map.size()
              << ", average size is " << (edges_map.empty() ? 0 : (total_set_size / edges_map.size()))
              << ", maximum size is " << max_set_size
              << std::endl;
    // Sort the points based on their length (from high to low)
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });

    std::vector<EdgeSet> points;
    std::vector<int64_t> counts;
    std::vector<std::vector<int128_t>> hashes_vec;
    points.reserve(n_points);
    counts.reserve(n_points);
    hashes_vec.reserve(n_points);
    for (auto& entry: pairs) {
        points.emplace_back(std::move(entry.first));
        counts.push_back(entry.second.count);
        hashes_vec.emplace_back(std::move(entry.second.hashes));
    }
    // Reorder the edges based on their frequency in each EdgeSet. Edge with low frequency comes first.
    for (auto& point: points) {
        std::sort(point.edges.begin(), point.edges.end(),
                  [&edge_counter](const auto& a, const auto& b) { return edge_counter[a] < edge_counter[b]; });
    }
    Clusterer clusterer(min_pts, epsilon);
    LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: started clustering\n";
    auto labels = clusterer.dbscan(points, counts, edge_counter);
    // Write to output column
    LOG(INFO) << "CELONIS_CLUSTER_VARIANTS: writing to column\n";
    if (!labels.has_value()) {
        ctx->set_error(std::string("CELONIS_CLUSTER_VARIANTS timeout").c_str(), false);
        return;
    }
    auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
    }
    if (fields[0]->is_nullable()) {
        down_cast<NullableColumn*>(fields[0].get())->null_column_data().emplace_back(0);
    }
    if (fields[1]->is_nullable()) {
        down_cast<NullableColumn*>(fields[1].get())->null_column_data().emplace_back(0);
    }
    auto hash_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(fields[0].get()));
    auto label_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(fields[1].get()));
    uint32_t n_elements = 0;
    phmap::flat_hash_set<int128_t> null_variant_hashes_seen;
    for (auto i = 0; i < n_points; ++i) {
        const auto label = labels->at(i);
        for (const int128_t hash128: hashes_vec[i]) {
            hash_col->elements_column()->append_datum(hash128);
            label_col->elements_column()->append_datum(label);
            ++n_elements;
            if (label == NULL_VARIANT_LABEL) {
                null_variant_hashes_seen.insert(hash128);
            }
        }
    }
    for (int128_t hash128: null_variant_hashes) {
        if (!null_variant_hashes_seen.contains(hash128)) {
            hash_col->elements_column()->append_datum(hash128);
            label_col->elements_column()->append_datum(NULL_VARIANT_LABEL);
            ++n_elements;
        }
    }
    auto& label_offsets = label_col->offsets_column()->get_data();
    auto& hash_offsets = hash_col->offsets_column()->get_data();
    label_offsets.push_back(label_offsets.back() + n_elements);
    hash_offsets.push_back(hash_offsets.back() + n_elements);
}

std::string ClusterVariantsAggregateFunction::get_name() const { return "celonis_cluster_variants"; }

} // namespace starrocks
