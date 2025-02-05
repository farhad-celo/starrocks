#pragma once

#include "column/hash_set.h"
#include "column/column_helper.h"
#include "column/object_column.h"
#include "column/type_traits.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/agg/variant_util.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"
#include "variant.h"
#include <set>
#include <boost/algorithm/string/join.hpp>

namespace starrocks {

struct CelonisVariantStatsAggregateV2State {

    using EdgeHashMap = phmap::flat_hash_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;

    void update(FunctionContext* ctx, const Column** columns, size_t row_num) {
        // Expects columns: [0] variant_column, [1] count_column
        // Pass a constant column with value 1 to ignore count.
        // NULL and NullableColumn are handled by NullableAggregateFunctionVariadic.
        int64_t count = 0;
        if (!columns[1]->is_constant()) {
            const auto& c_column = down_cast<const Int64Column&>(*columns[1]);
            count = c_column.get(row_num).get_int64();
        } else {
            const auto& c_column = down_cast<const ConstColumn&>(*columns[1]);
            count = c_column.get(0).get_int64();
        }
        // We pass all consts from update() through serialization.
        if (ctx->is_notnull_constant_column(3)) {
            edge_count_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(3));
        }
        if (ctx->is_notnull_constant_column(4)) {
            disable_top_variant_stats_ = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(4));
        }
        if (ctx->is_notnull_constant_column(5)) {
            enable_proto_encoding_ = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(5));
        }

        // DCHECK(ctx->is_notnull_constant_column(2));
        // treat columns[2] as constant.
        if (!activity_array_initialized_) {
            DCHECK(!columns[2]->empty());
            if (columns[2]->is_null(0)) {
              return;
            }
            // initialize activity_array: ignore NULLs and dedup.
            activity_array_initialized_ = true;
            HashSet<std::string> seen;
            activity_array_.clear();
            auto array = columns[2]->get(0).get_array();
            activity_array_.reserve(array.size());
            for (const auto& datum: array) {
                if (datum.is_null()) {
                    continue;
                }
                std::string activity = datum.get_slice().to_string();
                auto result = seen.insert(activity);
                if (result.second) {
                    activity_array_.push_back(activity);
                }
            }
            use_16bit_activity_ = activity_array_.size() <= static_cast<size_t>(std::numeric_limits<int16_t>::max());
            activity_stats_.clear();
            activity_stats_.resize(activity_array_.size());
        }

        if (columns[0]->is_null(row_num)) {
            return;
        }

        const ArrayColumn& activity_column = *(down_cast<const ArrayColumn*>(
                ColumnHelper::get_data_column(columns[0])));
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

        const Int32Column* activity_indexes = down_cast<const Int32Column*>(activity_elements);
        size_t n = c_offset[row_num + 1] - c_offset[row_num];
        std::vector<int32_t> variant;
        variant.reserve(n);
        bool bad_encoded = false;
        // A valid index should have: 0 <= idx <= max_idx;
        int32_t max_idx = static_cast<int32_t>(activity_array_.size()) - 1;

        for (size_t i = 0; i < n; i++) {
            size_t offset = c_offset[row_num] + i;
            if (activity_nulls != nullptr && (*activity_nulls)[offset]) {
                // ignore null activity index.
                continue;
            }
            auto idx = activity_indexes->get(offset).get_int32();
            if (idx < 0 || idx > max_idx) {
                bad_encoded = true;
                break;
            }
            variant.push_back(idx);
        }
        if (!bad_encoded && count != 0) {
            lengths_.push_back(variant.size());
            offsets_.push_back(offsets_.back() + variant.size());
            counts_.push_back(count);
            if (use_16bit_activity_) {
                std::transform(variant.begin(), variant.end(), std::back_inserter(activities_16bit_),
                               [](int32_t val) { return static_cast<int16_t>(val); });
            } else {
                activities_.insert(activities_.end(), variant.begin(), variant.end());
            }
            std::vector<bool> activity_updated_count_cases(activity_array_.size(), false);
            phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge> edge_updated_count_cases;
            // update activity_stats_ and edge_stats_
            for (auto i = 0; i < variant.size(); ++i) {
                auto activity_id = variant[i];
                ActivityStats& a_stats = activity_stats_[activity_id];
                a_stats.count += count;
                if (!activity_updated_count_cases[activity_id]) {
                    activity_updated_count_cases[activity_id] = true;
                    a_stats.count_case += count;
                }
                if (i == 0) {
                    a_stats.count_start += count;
                }
                if (i == variant.size() - 1) {
                    a_stats.count_end += count;
                }
                if (i > 0 && (edge_count_ >= 0 || variant[i - 1] == activity_id)) {
                    Edge e(variant[i - 1], activity_id);
                    auto& e_stats = edge_stats_[e];
                    e_stats.count += count;
                    auto [it, inserted] = edge_updated_count_cases.insert(e);
                    if (inserted) {
                        e_stats.count_case += count;
                    }
                }
            }
        }
    }

    // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const {
        DCHECK(activity_array_initialized_);
        size_t result = 0;
        // activity_array
        result += sizeof(size_t);        // length of activity_array
        result += sizeof(size_t);        // serialized_size of activities in activity_array: sum(len(activity) + 1 for activity in activity_array)
        result += activity_array_serialized_size();

        // activity_stats
        result += 4 * sizeof(size_t) * activity_array_.size();

        // edge_stats
        result += sizeof(size_t);        // number of unique edges
        result += (2 * sizeof(int32_t) + 2 * sizeof(size_t)) * edge_stats_.size();

        result += sizeof(int64_t);       // edge_count_
        result += sizeof(uint8_t);       // disable_top_variant_stats_
        result += sizeof(uint8_t);       // enable_proto_encoding_
        result += sizeof(size_t);        // num_variants
        result += sizeof(size_t);        // total_number_of_activities
        const size_t num_variants = lengths_.size();
        result += num_variants * sizeof(size_t);  // length1, length2, ...
        result += num_variants * sizeof(int64_t); // count1, count2, ...
        const size_t total = num_activities();
        result += total * activity_serialized_size();        // activities
        return result;
    }

    // Writes and binary encoded version of the object to dst.
    void serialize(uint8_t* dst) const {
        // Serialization format
        // length of activity_array
        // serialized_size of activity_array
        // activity1, activity2, ...
        // [activity_stats_1] count, count_case, count_start, count_end
        // [activity_stats_2] count, count_case, count_start, count_end
        // ...
        // num_edges
        // [edge_stats_1] src1, dst1, count, count_case
        // [edge_stats_2] src2, dst2, count, count_case
        // ...
        // edge_count
        // disable_top_variant_stats
        // enable_proto_encoding
        // num_variants
        // total_number_of_activities (i.e., length1 + length2 + ...)
        // length1, length2, ...
        // count1, count2, ...
        // [variant_1] activity_11, activity_12, ...
        // [variant_2] activity_21, activity_22, ...
        // ...
        size_t activity_array_length = activity_array_.size();
        memcpy(dst, &activity_array_length, sizeof(size_t));
        dst += sizeof(size_t);
        size_t total_activity_size = activity_array_serialized_size();
        memcpy(dst, &total_activity_size, sizeof(size_t));
        dst += sizeof(size_t);
        for (const auto& activity: activity_array_) {
            memcpy(dst, activity.data(), activity.size() + 1);
            dst += activity.size() + 1;
        }
        // activity_stats
        for (const auto& stats: activity_stats_) {
            memcpy(dst, &stats.count, sizeof(size_t));
            dst += sizeof(size_t);
            memcpy(dst, &stats.count_case, sizeof(size_t));
            dst += sizeof(size_t);
            memcpy(dst, &stats.count_start, sizeof(size_t));
            dst += sizeof(size_t);
            memcpy(dst, &stats.count_end, sizeof(size_t));
            dst += sizeof(size_t);
        }

        // edge_stats
        size_t num_edges = edge_stats_.size();
        memcpy(dst, &num_edges, sizeof(size_t));
        dst += sizeof(size_t);

        for (const auto& p: edge_stats_) {
            const auto& edge = p.first;
            const auto& stats = p.second;
            memcpy(dst, &edge.src, sizeof(int32_t));
            dst += sizeof(int32_t);
            memcpy(dst, &edge.dst, sizeof(int32_t));
            dst += sizeof(int32_t);
            memcpy(dst, &stats.count, sizeof(size_t));
            dst += sizeof(size_t);
            memcpy(dst, &stats.count_case, sizeof(size_t));
            dst += sizeof(size_t);
        }

        memcpy(dst, &edge_count_, sizeof(int64_t));
        dst += sizeof(int64_t);
        memcpy(dst, &disable_top_variant_stats_, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        memcpy(dst, &enable_proto_encoding_, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        const size_t num_variants = lengths_.size();
        DCHECK_EQ(num_variants, counts_.size());
        const size_t total = num_activities();
        memcpy(dst, &num_variants, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &total, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, lengths_.data(), sizeof(size_t) * lengths_.size());
        dst += sizeof(size_t) * lengths_.size();
        memcpy(dst, counts_.data(), sizeof(int64_t) * counts_.size());
        dst += sizeof(int64_t) * counts_.size();
        if (use_16bit_activity_) {
            memcpy(dst, activities_16bit_.data(), sizeof(int16_t) * activities_16bit_.size());
        } else {
            memcpy(dst, activities_.data(), sizeof(int32_t) * activities_.size());
        }
        dst += activity_serialized_size() * num_activities();
    }

    // Deserializes a CelonisVariantStatsV2AggregateState object and merges it with the current state.
    void deserialize_and_merge(const uint8_t* src, size_t len) {
        auto start_time = std::chrono::high_resolution_clock::now();
        merging_bytes_ += len;
        ++merging_states_;
        const uint8_t* end = src + len;
        size_t activity_array_length;
        memcpy(&activity_array_length, src, sizeof(size_t));
        src += sizeof(size_t);
        size_t total_activity_size;
        memcpy(&total_activity_size, src, sizeof(size_t));
        src += sizeof(size_t);
        if (activity_array_initialized_) {
            DCHECK_EQ(activity_array_.size(), activity_array_length);
            DCHECK_EQ(activity_array_serialized_size(), total_activity_size);
            src += total_activity_size;
        } else {
            activity_array_initialized_ = true;
            activity_array_.clear();
            activity_array_.reserve(activity_array_length);
            for (auto i = 0; i < activity_array_length; ++i) {
                std::string string_value = std::string(reinterpret_cast<const char*>(src));
                src += string_value.size() + 1;
                activity_array_.push_back(string_value);
            }
            use_16bit_activity_ = activity_array_.size() <= static_cast<size_t>(std::numeric_limits<int16_t>::max());
            activity_stats_.clear();
            activity_stats_.resize(activity_array_length);
        }
        // read activity_stats and merge it with existing stats
        for (auto i = 0; i < activity_array_length; ++i) {
            // [activity_stats] count, count_case, count_start, count_end
            size_t count = 0;
            size_t count_case = 0;
            size_t count_start = 0;
            size_t count_end = 0;
            memcpy(&count, src, sizeof(size_t));
            src += sizeof(size_t);
            memcpy(&count_case, src, sizeof(size_t));
            src += sizeof(size_t);
            memcpy(&count_start, src, sizeof(size_t));
            src += sizeof(size_t);
            memcpy(&count_end, src, sizeof(size_t));
            src += sizeof(size_t);
            auto& stats = activity_stats_[i];
            stats.count += count;
            stats.count_case += count_case;
            stats.count_start += count_start;
            stats.count_end += count_end;
        }

        size_t num_edges = 0;
        memcpy(&num_edges, src, sizeof(size_t));
        src += sizeof(size_t);
        for (auto i = 0; i < num_edges; ++i) {
            // [edge_stats] src1, dst1, count, count_case
            int32_t edge_src, edge_dst;
            memcpy(&edge_src, src, sizeof(int32_t));
            src += sizeof(int32_t);
            memcpy(&edge_dst, src, sizeof(int32_t));
            src += sizeof(int32_t);
            size_t count = 0;
            size_t count_case = 0;
            memcpy(&count, src, sizeof(size_t));
            src += sizeof(size_t);
            memcpy(&count_case, src, sizeof(size_t));
            src += sizeof(size_t);
            Edge e(edge_src, edge_dst);
            auto& stats = edge_stats_[e];
            stats.count += count;
            stats.count_case += count_case;
        }

        memcpy(&edge_count_, src, sizeof(int64_t));
        src += sizeof(int64_t);
        memcpy(&disable_top_variant_stats_, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        memcpy(&enable_proto_encoding_, src, sizeof(uint8_t));
        src += sizeof(uint8_t);

        size_t num_variants;
        memcpy(&num_variants, src, sizeof(size_t));
        src += sizeof(size_t);
        size_t total_activities;
        memcpy(&total_activities, src, sizeof(size_t));
        src += sizeof(size_t);

        // Based on a benchmark, memcpy is better than vector.insert and vector.insert is better than std::copy.
        auto old_lengths_size = lengths_.size();
        lengths_.resize(old_lengths_size + num_variants);
        memcpy(lengths_.data() + old_lengths_size, src, num_variants * sizeof(size_t));
        src += num_variants * sizeof(size_t);
        // Update offsets
        for (auto i = 0; i < num_variants; ++i) {
            offsets_.push_back(offsets_.back() + lengths_[old_lengths_size + i]);
        }

        auto old_counts_size = counts_.size();
        counts_.resize(old_counts_size + num_variants);
        memcpy(counts_.data() + old_counts_size, src, num_variants * sizeof(int64_t));
        src += num_variants * sizeof(int64_t);

        auto old_activities_size = num_activities();
        if (use_16bit_activity_) {
            activities_16bit_.resize(old_activities_size + total_activities);
            memcpy(activities_16bit_.data() + old_activities_size, src, total_activities * sizeof(int16_t));
            src += total_activities * sizeof(int16_t);
        } else {
            activities_.resize(old_activities_size + total_activities);
            memcpy(activities_.data() + old_activities_size, src, total_activities * sizeof(int32_t));
            src += total_activities * sizeof(int32_t);
        }
        DCHECK_EQ(src, end);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        merging_microseconds_ += duration.count();
    }

    int64_t edge_count() const { return edge_count_; }

    std::pair<size_t, size_t> get_offsets(size_t idx) const {
        DCHECK_EQ(offsets_.size(), lengths_.size() + 1);
        return {offsets_.at(idx), offsets_.at(idx + 1)};
    }

    bool no_activities() const {
        return activity_array_.empty();
    }

    bool no_variants() const {
        return lengths_.empty();
    }

    bool disable_top_variant_stats() const { return disable_top_variant_stats_; }

    bool enable_proto_encoding() const { return enable_proto_encoding_; }

    bool activity_array_initialized() const { return activity_array_initialized_; }

    const std::vector<size_t>& variant_lengths() const { return lengths_; }

    const std::vector<int64_t>& counts() const { return counts_; }

    int32_t get_activity(size_t index) const {
        return use_16bit_activity_ ? static_cast<int32_t>(activities_16bit_.at(index)) : activities_.at(index);
    }

    const std::vector<std::string>& activity_array() const { return activity_array_; }

    const std::vector<ActivityStats>& activity_stats() const { return activity_stats_; }

    const EdgeHashMap& edge_stats() const { return edge_stats_; }

    uint64_t merging_microseconds() const { return merging_microseconds_; }

    uint64_t merging_bytes() const { return merging_bytes_; }

    uint64_t merging_states() const { return merging_states_; }

    std::optional<std::string>
    json_string(const std::vector<std::vector<size_t>>& activity_top_variants, size_t happy) const;

    std::optional<std::string>
    base64_encoded_string(const std::vector<std::vector<size_t>>& activity_top_variants, size_t happy) const;

    std::optional<std::string>
    to_string(const std::vector<std::vector<size_t>>& activity_top_variants, size_t happy) const;

    // Computes the variant that starts and ends with the most common start/end activities,
    // otherwise returns the top most frequent activity.
    size_t compute_happy_variant(const std::vector<size_t>& sorted) const;

    // Computes top-10 variants for each activity and happy variant.
    void compute_top_variants(std::vector<std::vector<size_t>>& activity_top_variants, size_t& happy) const;

private:

    size_t activity_serialized_size() const {
        return use_16bit_activity_ ? sizeof(int16_t) : sizeof(int32_t);
    }

    size_t num_activities() const {
        return use_16bit_activity_ ? activities_16bit_.size() : activities_.size();
    }

    size_t activity_array_serialized_size() const {
        size_t result = 0;
        for (const auto& activity: activity_array_) {
            result += activity.size() + 1;
        }
        return result;
    }

    int64_t edge_count_ = (1LL << 32); // very large number to output all edges.
    bool disable_top_variant_stats_ = false;
    bool enable_proto_encoding_ = false;
    bool activity_array_initialized_ = false;
    // length of each variant
    std::vector<size_t> lengths_;
    // offsets of variants, if lengths_ = [1, 2, 3], offsets = [0, 1, 3, 6]
    std::vector<size_t> offsets_ = {0};
    // count of each variant
    std::vector<int64_t> counts_;
    // when use_16bit_activity_ is true, activities_16bit_ is populated, otherwise activities_ is populated.
    // The activities of i-th variant are in [offsets[i], offsets[i + 1]) of activities_ or activities_16bit_.
    std::vector<int32_t> activities_;
    std::vector<int16_t> activities_16bit_;
    std::vector<std::string> activity_array_;
    bool use_16bit_activity_ = false;
    // Since all the variants are assumed to be unique, we can compute activity and edge stats on leaves.
    std::vector<ActivityStats> activity_stats_;
    EdgeHashMap edge_stats_;
    // merging statistics
    uint64_t merging_microseconds_ = 0;
    uint64_t merging_bytes_ = 0;
    uint64_t merging_states_ = 0;
};

/**
 * @param: [ variant_column, count_column, activity_array, [, edge_count [, disable_top_variant_stats [, enable_proto_encoding ] ] ] ]
 * @paramType columns: [ ARRAY_INT, BIGINT, ARRAY_VARCHAR, [, BIGINT [, BOOLEAN [, BOOLEAN ] ] ] ]
 * @return: json or base64 encoded binary proto string
 * variant_column: Encoded variant. The implementation assumes the input variant_column does not contain duplicates.
 * count_column: Indicates the frequency of the variant.
 * activity_array: Activity array used to generate the encoding map.
 * edge_count (optional): Limits the size of the edge table. if edge_count <= 0, edge stats is not populated and output. default = 1LL << 32 to output all the edges.
 * disable_top_variant_stats (optional): Disables top variant stats, default = false.
 * enable_proto_encoding (optional): Enable base64 encoded binary proto output, default = false.
 *
 * Used to support PQL EXPLORE_PROCESS and GRAPH
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245719/EXPLORE+PROCESS
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11248519/GRAPH+Query
 * Below are the 3 use cases
 * 1. explore_process will pass in edge_count = -1. It needs self-loop stats but it does not need overall edge count or the edge table;
 * 2. graph with edge_count = 0. It needs overall edge count but does not need the edge table;
 * 3. graph with a positive edge_count. It needs overall edge count and the edge table (trimmed by edge count).
 */
class CelonisVariantStateV2AggregationFunction final
        : public AggregateFunctionBatchHelper<CelonisVariantStatsAggregateV2State,
                CelonisVariantStateV2AggregationFunction> {
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

private:

    std::string log_prefix(const std::string& query_id) const;

};

} // namespace starrocks
