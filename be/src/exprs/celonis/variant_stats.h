#pragma once

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/hash_set.h"
#include "column/object_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/variant.h"
#include "exprs/function_context.h"
#include "gutil/casts.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace starrocks {

// A pair of activities that appear together in a variant.
struct Edge {
    size_t hash;
    int32_t src;
    int32_t dst;

    Edge(int32_t in_src, int32_t in_dst, size_t hash_src, size_t hash_dst) {
        src = in_src;
        dst = in_dst;
        hash = hash_src;
        HashUtil::hash_combine(hash, hash_dst);
    }

    rapidjson::Value to_json(rapidjson::Document::AllocatorType& allocator) const;

    std::string debug_string() const;
};

struct EqualOnEdge {
    bool operator()(const Edge& x, const Edge& y) const {
        return x.hash == y.hash && x.src == y.src && x.dst == y.dst;
    }
};

struct HashOnEdge {
    std::size_t operator()(const Edge& x) const { return x.hash; }
};

// Basic statistics on an Edge.
struct EdgeStats {
    size_t count{0};  // Number of times this edge appears
    size_t count_case{0};  // Number distinct cases this edge appears in

    static size_t serialize_size() {
        return sizeof(size_t) * 2;
    }

    bool equal(const EdgeStats& other) {
        return count == other.count && count_case == other.count_case;
    }

    void merge(const EdgeStats& other);

    void serialize(uint8_t* dst) const;

    void deserialize(const uint8_t* src);

    rapidjson::Value to_json(rapidjson::Document::AllocatorType& allocator) const;

    std::string debug_string() const;
};

// Basic statistics on an activity.
struct ActivityStats {
    size_t count{0};  // Number of times the activity appears (can be > 1 per case)
    size_t count_case{0};  // Number of distinct cases that contain the activity.
    size_t count_start{0};  // Number of times the activity appears at the start of a case
    size_t count_end{0};  // Number of times the activity appears at the end of a case

    static size_t serialize_size() {
        return sizeof(size_t) * 4;
    }

    bool equal(const ActivityStats& other) {
        return count == other.count
        && count_case == other.count_case
        && count_start == other.count_start
        && count_end == other.count_end;
    }

    void merge(const ActivityStats& other);

    void serialize(uint8_t* dst) const;

    void deserialize(const uint8_t* src);

    rapidjson::Value to_json(rapidjson::Document::AllocatorType& allocator) const;

    std::string debug_string() const;
};

class VariantStatsState {
public:
    using SliceHashMap = phmap::flat_hash_map<SliceWithHash, int32_t, HashOnSliceWithHash, EqualOnSliceWithHash>;

    VariantStatsState() = default;

    ~VariantStatsState() = default;

    // Adds a variant with weight to the stats.
    size_t update(MemPool* mem_pool, const ArrayColumn& activity_column, size_t row_num, int64_t weight);

        // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const;

    // Writes and binary encoded version of the object to dst.
    // The size written will be serialized_size()
    void serialize(uint8_t* dst) const;

    // Deserializes a VariantStatsState object and merges it with the current state.
    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len);

    // Finalizes the state and returns a json string representing the result.
    // TODO(j.kim): Separate for VariantStats and InductiveMiner.
    std::string finalize() const;

    const SliceHashMap& get_activity_map()  const { return activity_map; }
    const VariantHashMap& get_variant_map() const { return variant_map; }

private:
    using EdgeHashMap = phmap::flat_hash_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;
    using EdgeHashSet = phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge>;
    using ActivityVector = std::vector<ActivityStats>;

    // Holds a reference (iterator) to a variant in the VariantHashMap and the count of the variant.
    using VRef = std::pair<VariantHashMap::const_iterator, size_t>;

    // List of variant references, used to hold top-k variants per activity.
    using VList = std::vector<VRef>;

    // Adds an activity to the dictionary if it does not exist.
    // Updates memory with the number of bytes allocated in mem_pool.
    // Returns the index of the activity and the hash.
    std::pair<int32_t, size_t> maybe_add_activity(MemPool* mem_pool, const Slice& slice, size_t& memory);

    // Computes the variant that starts and ends with the most common start/end activities,
    // otherwise returns the top most frequent activity.
    int compute_happy_variant(const std::vector<VRef>& sorted) const;

    // Computes top-10 variants for each activity.
    int compute_top_variants(std::vector<VList>& activity_top_variants, VRef& happy) const;

    std::string json_string(std::vector<VList>& activity_top_variants, VRef& happy) const;

    std::string debug_string() const;

    SliceHashMap activity_map; // activity -> index
    ActivityVector activity_stats; // activity_idx -> activity_stats
    EdgeHashMap edge_map; // edge -> edge_stats
    VariantHashMap variant_map; // variant -> count
};

// TODO(hagonzal): Return json column. Now it returns a string column with json.
// TODO(hagonzal): use templates and constexpr to try to remove the nullable/const ifs.
// TODO(hagonzal): add option to compute approximate top-k variants, now it returns exact top-k.

// TODO(j.kim): Refactor and make a template base class for VariantStats and InductiveMiner. For InductiveMiner, skip
// stats and forward IMFD_frequency_threshold.
class VariantStatsAggregateFunction
        : public AggregateFunctionBatchHelper<VariantStatsState, VariantStatsAggregateFunction> {
public:
    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const  {
        // Expects columns: [0] variant_column, [1] weight_column
        // Pass a constant column with value 1 to ignore weight.

        if (columns[0]->is_nullable() && columns[0]->is_null(row_num)) {
            return;
        }
        if (columns[1]->is_nullable() && columns[1]->is_null(row_num)) {
            return;
        }
        int64_t weight = 0;
        if (!columns[1]->is_constant()) {
            const auto& w_column = down_cast<const Int64Column&>(*columns[1]);
            weight = w_column.get(row_num).get_int64();
        } else {
            const auto& w_column = down_cast<const ConstColumn&>(*columns[1]);
            weight = w_column.get(0).get_int64();
        }
        const ArrayColumn& activity_column = down_cast<const ArrayColumn&>(*columns[0]);
        this->data(state).update(ctx->mem_pool(), activity_column, row_num, weight);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        // merge internal state with column[row_num]
        // the column type is binary
        DCHECK(column->is_binary());
        const auto* input_column = down_cast<const BinaryColumn*>(column);
        Slice slice = input_column->get_slice(row_num);
        size_t mem_usage = 0;
        mem_usage += this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*) slice.data,
                                                             slice.size);
        ctx->add_mem_usage(mem_usage);
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        // append our serialized state to column "to"
        auto* column = down_cast<BinaryColumn*>(to);
        size_t old_size = column->get_bytes().size();
        size_t new_size = old_size + this->data(state).serialized_size();
        column->get_bytes().resize(new_size);
        this->data(state).serialize(column->get_bytes().data() + old_size);
        column->get_offset().emplace_back(new_size);
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        // Used for streaming aggregation. Not implemented.
        DCHECK(false) << "convert_to_serialize_format is not supported";
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        std::string s = this->data(state).finalize();
        down_cast<BinaryColumn*>(to)->append(s);
    }

    std::string get_name() const override { return "celonis_variant_stats"; }
};

} // namespace starrocks
