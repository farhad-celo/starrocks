#pragma once

#include "column/datum.h"
#include "column/hash_set.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/variant.h"
#include "exprs/function_context.h"

namespace starrocks {

class VariantAggregateState {
public:
    using SliceHashMap = phmap::flat_hash_map<SliceWithHash, int32_t, HashOnSliceWithHash, EqualOnSliceWithHash>;

    VariantAggregateState() = default;

    ~VariantAggregateState() = default;

    // Adds a variant with weight to the stats.
    size_t update(MemPool* mem_pool, const ArrayColumn& activity_column, size_t row_num, int64_t weight);

    // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const;

    // Writes and binary encoded version of the object to dst.
    // The size written will be serialized_size()
    void serialize(uint8_t* dst) const;

    // Deserializes a VariantAggregateState object and merges it with the current state.
    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len);

    const SliceHashMap& activity_map() const { return activity_map_; }
    const VariantHashMap& variant_map() const { return variant_map_; }

private:
    // Adds an activity to the dictionary if it does not exist.
    // Updates memory with the number of bytes allocated in mem_pool.
    // Returns the index of the activity and the hash.
    std::pair<int32_t, size_t> maybe_add_activity(MemPool* mem_pool, const Slice& slice, size_t* memory);

    std::string debug_string() const;

    SliceHashMap activity_map_;  // activity -> index
    VariantHashMap variant_map_; // variant -> count
};

// Helper class to finalize VariantAggregateFunction.
class VariantAggregateFinalizer {
public:
    VariantAggregateFinalizer(FunctionContext* ctx, const VariantAggregateState& state)
            : ctx_(ctx), activity_map_(state.activity_map()), variant_map_(state.variant_map()) {}

    // Finalizes the state and returns a json string representing the result.
    virtual std::string finalize() = 0;

protected:
    FunctionContext* ctx_;
    const VariantAggregateState::SliceHashMap& activity_map_;
    const VariantHashMap& variant_map_;
};

// Aggregates distinct variants and their counts.
// A derived class must provide a custom VariantAggregateFinalizer which implements finalize().
class VariantAggregateFunction : public AggregateFunctionBatchHelper<VariantAggregateState, VariantAggregateFunction> {
public:
    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const final;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const final;

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const final;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const final {
        // Used for streaming aggregation. Not implemented.
        DCHECK(false) << "convert_to_serialize_format is not supported";
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const final;

    // Returns a VariantAggregateFinalizer instance.
    virtual std::unique_ptr<VariantAggregateFinalizer> get_finalizer(FunctionContext* ctx,
                                                                     const VariantAggregateState& state) const = 0;
};

} // namespace starrocks
