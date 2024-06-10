#pragma once

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/const_column.h"
#include "column/column_helper.h"
#include "column/hash_set.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"
#include "variant.h"
#include "variant_agg.h"
#include <boost/functional/hash.hpp>

namespace starrocks {

class ClusterVariantsState {
public:
    ClusterVariantsState() {}

    ~ClusterVariantsState() {}

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
        const ArrayColumn& activity_column = *(down_cast<const ArrayColumn*>(
                ColumnHelper::get_data_column(variant_column)));
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
        // Add the variant into the variant_map.
        variant_map_[variant] += 1;
        hash_map_[variant.hash] = hash128;

        return memory;
    }

    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(uint32_t);// num_null_variant_hashes
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

        // variants
        result += sizeof(uint32_t); // num_variants
        for (auto it = variant_map_.begin(); it != variant_map_.end(); it++) {
            result += sizeof(int128_t);                         // 128 bits hash
            result += sizeof(size_t);                           // count
            result += sizeof(uint32_t);                         // num_activities
            result += sizeof(uint32_t) * it->first.data.size(); // activity indices
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
        for (int128_t hash128: null_variant_hashes_) {
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

        // variants
        uint32_t num_variants = variant_map_.size();
        memcpy(dst, &num_variants, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (auto it = variant_map_.begin(); it != variant_map_.end(); it++) {
            size_t count = it->second;
            int128_t hash128 = hash_map_.find(it->first.hash)->second;
            uint32_t length = it->first.data.size();

            memcpy(dst, &hash128, sizeof(int128_t));
            dst += sizeof(int128_t);
            memcpy(dst, &count, sizeof(size_t));
            dst += sizeof(size_t);
            memcpy(dst, &length, sizeof(uint32_t));
            dst += sizeof(uint32_t);

            for (int i = 0; i < it->first.data.size(); i++) {
                uint32_t idx = it->first.data[i];
                memcpy(dst, &idx, sizeof(uint32_t));
                dst += sizeof(uint32_t);
            }
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
                uint32_t len;
                uint32_t idx;
                memcpy(&idx, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                memcpy(&len, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                Slice s(src, len);
                src += len;
                index_vector[idx] = maybe_add_activity(mem_pool, s, &mem);
            }

            // variants
            uint32_t num_variants;
            memcpy(&num_variants, src, sizeof(uint32_t));
            src += sizeof(uint32_t);

            for (int i = 0; i < num_variants; i++) {
                size_t count;
                int128_t hash128;
                uint32_t num_activities;
                memcpy(&hash128, src, sizeof(int128_t));
                src += sizeof(int128_t);
                memcpy(&count, src, sizeof(size_t));
                src += sizeof(size_t);
                memcpy(&num_activities, src, sizeof(uint32_t));
                src += sizeof(uint32_t);

                Variant variant(num_activities);
                for (int j = 0; j < num_activities; j++) {
                    uint32_t idx;
                    memcpy(&idx, src, sizeof(uint32_t));
                    src += sizeof(uint32_t);
                    auto pair = index_vector[idx];
                    variant.add(pair.first, pair.second);
                }
                variant_map_[variant] += count;
                hash_map_[variant.hash] = hash128;
            }
        }
        return mem;
    }

    int64_t min_pts() const { return min_pts_; }

    int64_t epsilon() const { return epsilon_; }

    const SliceHashMap& activity_map() const { return activity_map_; }

    const VariantHashMap& variant_map() const { return variant_map_; }

    const phmap::flat_hash_map<size_t, int128_t> hash_map() const { return hash_map_; }

    const phmap::flat_hash_set<int128_t> null_variant_hashes() const { return null_variant_hashes_; }

private:
    // Adds an activity to the dictionary if it does not exist.
    // Updates memory with the number of bytes allocated in mem_pool.
    // Returns the index of the activity and the hash.
    std::pair<int32_t, size_t> maybe_add_activity(MemPool* mem_pool, const Slice& slice, size_t* memory);

    int64_t min_pts_ = 0;
    int64_t epsilon_ = 0;
    SliceHashMap activity_map_;  // activity -> index
    VariantHashMap variant_map_; // variant -> count
    phmap::flat_hash_map<size_t, int128_t> hash_map_; // variant hash -> input hash value
    // We need to keep track of hash values of variant = NULL. This is because variant = [] or [NULL, ...] may have
    // the different hash value as variant = NULL.
    phmap::flat_hash_set<int128_t> null_variant_hashes_;
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
 * 3. variant does not contain non-NULL activities
 * The implementation assumes that case 2 and case 3 have the same hash value.
 * For the other clusters, cluster_id is a nonnegative integer (e.g., 0, 1, 2).
 *
 * Used to support PQL CLUSTER_VARIANTS
 * https://docs.celonis.com/en/cluster_variants.html
 */
class ClusterVariantsAggregateFunction
        : public AggregateFunctionBatchHelper<ClusterVariantsState, ClusterVariantsAggregateFunction> {
public:

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    std::string get_name() const override;
};

} // namespace starrocks
