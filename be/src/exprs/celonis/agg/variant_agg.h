#pragma once

#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/const_column.h"
#include "column/datum.h"
#include "column/hash_set.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "variant.h"

namespace starrocks {

class VariantAggregateState {
public:
    VariantAggregateState() = default;

    virtual ~VariantAggregateState() = default;

    // Adds a variant with weight to the stats.
    virtual size_t update(FunctionContext* ctx, const Column** columns, size_t row_num);

    // Returns the total size in bytes required to encode this object.
    virtual size_t serialized_size() const;

    // Writes and binary encoded version of the object to dst.
    // The size written will be serialized_size()
    virtual void serialize(uint8_t* dst) const;

    // Deserializes a VariantAggregateState object and merges it with the current state.
    virtual size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len);

    const SliceHashMap& activity_map() const { return activity_map_; }
    const VariantHashMap& variant_map() const { return variant_map_; }

private:
    // Adds an activity to the dictionary if it does not exist.
    // Updates memory with the number of bytes allocated in mem_pool.
    // Returns the index of the activity and the hash.

    std::string debug_string() const;

    SliceHashMap activity_map_;  // activity -> index
    VariantHashMap variant_map_; // variant -> count
};

// Helper class to finalize VariantAggregateFunction.
class VariantAggregateFinalizer {
public:
    VariantAggregateFinalizer(FunctionContext* ctx, const VariantAggregateState& state)
            : ctx_(ctx), activity_map_(state.activity_map()), variant_map_(state.variant_map()) {}

    virtual ~VariantAggregateFinalizer() = default;

    // Finalizes the state and returns a string representing the result.
    // When there is an exception, set ctx accordingly and return std::nullopt.
    virtual std::optional<std::string> finalize(FunctionContext* ctx) = 0;

protected:
    FunctionContext* ctx_;
    const SliceHashMap& activity_map_;
    const VariantHashMap& variant_map_;
};

// Aggregates distinct variants and their counts.
// A derived class must provide a custom VariantAggregateFinalizer which implements finalize().
template <typename State>
class VariantAggregateFunction : public AggregateFunctionBatchHelper<State, VariantAggregateFunction<State>> {
public:
    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const final {
        this->data(state).update(ctx, columns, row_num);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const final {
        // merge internal state with column[row_num]
        // the column type is binary
        if (column->is_nullable() && column->is_null(row_num)) {
            return;
        }
        const auto* data_column = ColumnHelper::get_data_column(column);
        DCHECK(data_column->is_binary());
        const auto* input_column = down_cast<const BinaryColumn*>(data_column);
        Slice slice = input_column->get_slice(row_num);
        size_t mem_usage = 0;
        mem_usage += this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*)slice.data, slice.size);
        ctx->add_mem_usage(mem_usage);
    }

    bool support_nullable_immediate_input() const final { return true; }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const final {
        // append our serialized state to column "to"
        auto* column = down_cast<BinaryColumn*>(to);
        size_t old_size = column->get_bytes().size();
        size_t new_size = old_size + this->data(state).serialized_size();
        column->get_bytes().resize(new_size);
        this->data(state).serialize(column->get_bytes().data() + old_size);
        column->get_offset().emplace_back(new_size);
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const final {
        // Used for streaming aggregation. Not implemented.
        throw std::runtime_error("variant aggregate: convert_to_serialize_format not supported");
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const final {
        auto finalizer = get_finalizer(ctx, this->data(state));
        std::optional<std::string> s = finalizer->finalize(ctx);
        if (s.has_value()) {
            down_cast<BinaryColumn*>(to)->append(s.value());
        }
    }

    // Returns a VariantAggregateFinalizer instance.
    virtual std::unique_ptr<VariantAggregateFinalizer> get_finalizer(FunctionContext* ctx,
                                                                     const State& state) const = 0;
};

} // namespace starrocks
