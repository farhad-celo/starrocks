#pragma once

#include <boost/functional/hash.hpp>

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/const_column.h"
#include "column/hash_set.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"
#include "variant.h"
#include "variant_agg.h"

namespace starrocks {

static const int32_t DUMMY_NODE = -1;

// One edge sets can correspond to multiple variants
struct EdgeSet {
    size_t hash{0};
    bool is_empty_variant = false;
    std::vector<Edge> edges;

    EdgeSet() = default;

    EdgeSet(const std::vector<Edge>& input_edges) {
        edges = input_edges;
        if (edges.size() == 1) {
            is_empty_variant = true;
        }
        compute_and_set_hash();
    }

    EdgeSet(const Variant& variant) {
        phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge> edge_set;
        if (variant.data.empty()) {
            is_empty_variant = true;
        }
        int32_t pre_node = DUMMY_NODE;
        for (auto node : variant.data) {
            edge_set.insert({pre_node, node});
            pre_node = node;
        }
        edge_set.insert({pre_node, DUMMY_NODE});
        edges.reserve(edge_set.size());
        std::copy(edge_set.begin(), edge_set.end(), std::back_inserter(edges));
        compute_and_set_hash();
    }

    size_t size() const { return edges.size(); }

    std::string debug_string() const {
        std::stringstream ss;
        ss << "edges (" << edges.size() << ") [";
        std::string sep = "";
        for (int i = 0; i < edges.size(); i++) {
            ss << sep << "(" << edges[i].src << "," << edges[i].dst << ")";
            sep = ",";
        }
        ss << "] hash " << hash;
        return ss.str();
    }

private:
    void compute_and_set_hash() {
        std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            if (a.src != b.src) {
                return a.src < b.src;
            } else {
                return a.dst < b.dst;
            }
        });
        for (const auto& edge : edges) {
            boost::hash_combine(hash, edge.hash);
        }
    }
};

struct EqualOnEdgeSet {
    bool operator()(const EdgeSet& x, const EdgeSet& y) const {
        if (x.hash != y.hash) {
            return false;
        }
        if (x.edges.size() != y.edges.size()) {
            return false;
        }
        const auto size = x.edges.size();
        for (auto i = 0; i < size; ++i) {
            if (x.edges[i].src != y.edges[i].src || x.edges[i].dst != y.edges[i].dst) {
                return false;
            }
        }
        return true;
    }
};

struct HashOnEdgeSet {
    std::size_t operator()(const EdgeSet& x) const { return x.hash; }
};

class ClusterVariantsState {
public:
    ClusterVariantsState() = default;

    ~ClusterVariantsState() = default;

    size_t update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        // _const_columns in merge is not aligned with _arg_types. So we pass all consts from
        // update() through serialization.
        if (ctx->is_notnull_constant_column(2)) {
            min_pts_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(2));
        }
        if (ctx->is_notnull_constant_column(3)) {
            epsilon_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(3));
        }
        if (min_pts_ < 0) {
            std::string msg = "MIN_PTS must be non-negative, however it is " + std::to_string(min_pts_);
            ctx->set_error(msg.c_str(), false);
            return 0;
        }
        if (epsilon_ < 0 || epsilon_ > 5) {
            std::string msg = "EPSILON must be in the range [0, 5], however it is " + std::to_string(epsilon_);
            ctx->set_error(msg.c_str(), false);
            return 0;
        }
        // Expects columns: [0] variant_column, [1] hash_column
        const Column* hash_column = columns[1];
        if (hash_column->is_nullable()) {
            const NullableColumn* nullable_hash_column = down_cast<const NullableColumn*>(hash_column);
            hash_column = nullable_hash_column->data_column().get();
        }
        if (columns[1]->is_null(row_num)) {
            ctx->set_error(std::string("hash column should not contain NULL").c_str(), false);
            return 0;
        }
        const Int128Column& hashes = *(down_cast<const Int128Column*>(ColumnHelper::get_data_column(hash_column)));
        int128_t hash128 = hashes.get(row_num).get_int128();
        const Column* variant_column = columns[0];
        if (variant_column->is_nullable()) {
            const NullableColumn* nullable_variant_column = down_cast<const NullableColumn*>(variant_column);
            variant_column = nullable_variant_column->data_column().get();
        }
        if (columns[0]->is_null(row_num)) {
            null_variant_hashes_.insert(hash128);
            return 0;
        }
        const ArrayColumn& activity_column =
                *(down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(variant_column)));
        const UInt32Column::Container& c_offset = activity_column.offsets().get_data();
        const Column* activity_elements = &activity_column.elements();
        const NullableColumn* nc = dynamic_cast<const NullableColumn*>(activity_elements);
        const NullColumn::Container* activity_nulls = nullptr;

        if (activity_elements->has_null()) {
            activity_nulls = &(nc->null_column()->get_data());
        }

        if (nc != nullptr) {
            activity_elements = nc->data_column().get();
        }

        const BinaryColumn* b_elements = down_cast<const BinaryColumn*>(activity_elements);
        size_t memory = 0;
        size_t n = c_offset[row_num + 1] - c_offset[row_num];
        Variant variant(n);

        for (size_t i = 0; i < n; i++) {
            size_t offset = c_offset[row_num] + i;
            if (activity_nulls != nullptr && (*activity_nulls)[offset]) {
                // ignore NULLs
                continue;
            }
            auto idx_hash = maybe_add_activity(ctx->mem_pool(), b_elements->get_slice(offset), &memory);
            variant.add(idx_hash.first, idx_hash.second);
        }
        // Add the variant to edge_set_map.
        auto& edge_count = edge_set_map_[hash128];
        if (edge_count.second == 0) {
            EdgeSet edge_set(variant);
            edge_count.first = edge_set;
        }
        ++edge_count.second;
        return memory;
    }

    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(uint32_t); // num_null_variant_hashes
        result += sizeof(int128_t) * null_variant_hashes_.size();
        result += sizeof(int64_t); // min_pts_
        result += sizeof(int64_t); // epsilon_

        result += sizeof(uint32_t); // num_activities
        // activities dictionary
        for (auto it = activity_map_.begin(); it != activity_map_.end(); it++) {
            result += sizeof(uint32_t); // idx
            result += sizeof(uint32_t); // size
            result += it->first.size;   // data
        }
        // edge_sets
        result += sizeof(uint32_t); // num_edge_sets
        for (auto it = edge_set_map_.begin(); it != edge_set_map_.end(); it++) {
            result += sizeof(int128_t);                                    // 128 bits hash
            result += sizeof(uint32_t);                                    // num_edges
            result += 2 * sizeof(int32_t) * it->second.first.edges.size(); // edges
            result += sizeof(int64_t);                                     // count
        }

        return result;
    };

    void serialize(uint8_t* dst) const {
        // serialization format:
        // num_null_variant_hashes
        // null_variant_hashes1, null_variant_hashes2, ...
        // min_pts_
        // epsilon_
        // num_activities
        // (idx size activity) (idx size activity) ...
        // num_variants
        // hash128 count num_activities (activity_idx) (activity_idx) ...
        // hash128 count num_activities (activity_idx) (activity_idx) ...
        const uint32_t num_null_variant_hashes = null_variant_hashes_.size();
        memcpy(dst, &num_null_variant_hashes, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (int128_t hash128 : null_variant_hashes_) {
            memcpy(dst, &hash128, sizeof(int128_t));
            dst += sizeof(int128_t);
        }
        memcpy(dst, &min_pts_, sizeof(int64_t));
        dst += sizeof(int64_t);
        memcpy(dst, &epsilon_, sizeof(int64_t));
        dst += sizeof(int64_t);

        // activities dictionary
        uint32_t num_activities = activity_map_.size();
        memcpy(dst, &num_activities, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (auto it = activity_map_.begin(); it != activity_map_.end(); it++) {
            uint32_t size = it->first.size;
            uint32_t idx = it->second;
            memcpy(dst, &idx, sizeof(uint32_t));
            dst += sizeof(uint32_t);
            memcpy(dst, &size, sizeof(uint32_t));
            dst += sizeof(uint32_t);
            memcpy(dst, it->first.data, it->first.size);
            dst += it->first.size;
        }

        // edge_sets
        uint32_t num_edge_sets = edge_set_map_.size();
        memcpy(dst, &num_edge_sets, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (auto it = edge_set_map_.begin(); it != edge_set_map_.end(); it++) {
            int128_t hash128 = it->first;
            memcpy(dst, &hash128, sizeof(int128_t));
            dst += sizeof(int128_t);

            const auto& edge_set = it->second.first;
            uint32_t num_edges = edge_set.edges.size();
            memcpy(dst, &num_edges, sizeof(uint32_t));
            dst += sizeof(uint32_t);

            for (const auto& edge : edge_set.edges) {
                memcpy(dst, &edge.src, sizeof(int32_t));
                dst += sizeof(int32_t);
                memcpy(dst, &edge.dst, sizeof(int32_t));
                dst += sizeof(int32_t);
            }

            int64_t count = it->second.second;
            memcpy(dst, &count, sizeof(int64_t));
            dst += sizeof(int64_t);
        }
    }

    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) {
        // read from src and merge with existing state.
        uint32_t num_null_variant_hashes;
        memcpy(&num_null_variant_hashes, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        len -= sizeof(uint32_t);
        for (auto i = 0; i < num_null_variant_hashes; ++i) {
            int128_t hash128;
            memcpy(&hash128, src, sizeof(int128_t));
            null_variant_hashes_.insert(hash128);
            src += sizeof(int128_t);
            len -= sizeof(int128_t);
        }
        memcpy(&min_pts_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        len -= sizeof(int64_t);
        memcpy(&epsilon_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        len -= sizeof(int64_t);
        size_t mem = 0;
        const uint8_t* end = src + len;

        // src can contain multiple serialized states. We merge each of them.
        while (src < end) {
            // activities dictionary
            uint32_t num_activities;
            memcpy(&num_activities, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            // Maps input activity dictionary to the current activity dictionary.
            // src_index -> (local_idx, local_hash)
            std::vector<std::pair<int32_t, size_t>> index_vector;
            index_vector.resize(num_activities);
            for (uint32_t i = 0; i < num_activities; i++) {
                uint32_t slice_len;
                uint32_t idx;
                memcpy(&idx, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                memcpy(&slice_len, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                Slice s(src, slice_len);
                src += slice_len;
                index_vector[idx] = maybe_add_activity(mem_pool, s, &mem);
            }

            // edge_sets
            uint32_t num_edge_sets;
            memcpy(&num_edge_sets, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            for (int i = 0; i < num_edge_sets; i++) {
                int128_t hash128;
                uint32_t num_edges;
                int64_t count;
                memcpy(&hash128, src, sizeof(int128_t));
                src += sizeof(int128_t);
                memcpy(&num_edges, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                std::vector<Edge> edges;
                edges.reserve(num_edges);
                for (auto j = 0; j < num_edges; ++j) {
                    int32_t edge_src;
                    int32_t edge_dst;
                    memcpy(&edge_src, src, sizeof(int32_t));
                    src += sizeof(int32_t);
                    memcpy(&edge_dst, src, sizeof(int32_t));
                    src += sizeof(int32_t);
                    edge_src = (edge_src == DUMMY_NODE) ? DUMMY_NODE : index_vector[edge_src].first;
                    edge_dst = (edge_dst == DUMMY_NODE) ? DUMMY_NODE : index_vector[edge_dst].first;
                    edges.emplace_back(edge_src, edge_dst);
                }
                memcpy(&count, src, sizeof(int64_t));
                src += sizeof(int64_t);
                auto& edge_set_count = edge_set_map_[hash128];
                edge_set_count.first = EdgeSet(edges);
                edge_set_count.second += count;
            }
        }
        return mem;
    }

    int64_t min_pts() const { return min_pts_; }

    int64_t epsilon() const { return epsilon_; }

    const SliceHashMap& activity_map() const { return activity_map_; }

    const phmap::flat_hash_map<int128_t, std::pair<EdgeSet, int64_t>, StdHash<int128_t>>& edge_set_map() const {
        return edge_set_map_;
    }

    const HashSet<int128_t>& null_variant_hashes() const { return null_variant_hashes_; }

private:
    // Adds an activity to the dictionary if it does not exist.
    // Updates memory with the number of bytes allocated in mem_pool.
    // Returns the index of the activity and the hash.
    std::pair<int32_t, size_t> maybe_add_activity(MemPool* mem_pool, const Slice& slice, size_t* memory);

    int64_t min_pts_ = 0;
    int64_t epsilon_ = 0;
    phmap::flat_hash_map<int128_t, std::pair<EdgeSet, int64_t>, StdHash<int128_t>>
            edge_set_map_;      // variant_hash128 -> (edge_set, count)
    SliceHashMap activity_map_; // activity -> index
    // We need to keep track of hash values of variant = NULL. This is because variant = [] or [NULL, ...] may have
    // the different hash value as variant = NULL.
    HashSet<int128_t> null_variant_hashes_;
};

/**
 * @param: [ variant_column, hash_column, MIN_PTS, EPSILON ]
 * @paramType columns: [ ARRAY_VARCHAR, LARGEINT, BIGINT, BIGINT]
 * @return: STRUCT(hash: ARRAY_LARGEINT, cluster_id: ARRAY_BIGINT)
 * variant_column : variant is an array of string activities.
 * hash_column: 128 bits hash of the variant.
 * MIN_PTS: Minimal density of similar variants that is required to create a cluster. Lower values tend to create more
 *          clusters, while higher values tend to classify more variants as noise.
 * EPSILON: Search radius for measuring the variant density. The value must be in the range [0, 5].
 *
 * cluster_id = -1 for noise variants.
 * cluster_id = -2 for the below 3 cases
 * 1. variant is NULL
 * 2. variant is an empty array (i.e., [])
 * 3. variant does not contain non-NULL activities (i.e., [NULL, NULL])
 * For the other clusters, cluster_id is a non-negative integer (e.g., 0, 1, 2).
 *
 * Clustering is performed on the set representations of variants. The symmetric difference is then used to measure the
 * distance between these set representations.
 *
 * The process of converting a variant to its set representation involves:
 * 1. Filter out any NULL activities.
 * 2. Introduce a placeholder source activity at the start and a placeholder target activity at the end.
 * 3. Generate edges by considering each pair of adjacent activities.
 * 4. Eliminate any duplicate edges.
 *
 * Two different variants may share the same set representation.
 *
 *  For example, given the following variant = [A, B, NULL, C, NULL, C, C, D, A, B].
 * 1. NULL activity removal: [A, B, C, C, C, D, A, B]
 * 2. Addition of placeholder 'SRC' and 'TGT' activity: [SRC, A, B, C, C, C, D, A, B, TGT]
 * 3. Edge computation from adjacent activity pairs: [SRC-A, A-B, B-C, C-C, C-C, C-D, D-A, A-B, B-TGT]
 * 4. Deduplication of edges: [SRC-A, A-B, B-C, C-C, C-D, D-A, B-TGT]
 *
 * Used to support PQL CLUSTER_VARIANTS
 * https://docs.celonis.com/en/cluster_variants.html
 */
class ClusterVariantsAggregateFunction
        : public AggregateFunctionBatchHelper<ClusterVariantsState, ClusterVariantsAggregateFunction> {
public:
    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    bool support_nullable_immediate_input() const override { return true; }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    std::string get_name() const override;
};

} // namespace starrocks
