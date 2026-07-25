#pragma once

#include "column/column_helper.h"
#include "column/object_column.h"
#include "column/type_traits.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "runtime/mem_pool.h"

namespace starrocks {

template <LogicalType LT>
struct CelonisHistogramBoundariesAggregateState {
    using CppType = RunTimeCppType<LT>;
    using CounterType = RunTimeCppType<TYPE_BIGINT>;
    using BucketMap = std::conditional_t<IsSlice<CppType>, std::map<Slice, CounterType, Slice::Comparator>,
                                         std::map<CppType, CounterType>>;

    auto may_insert_boundary(MemPool* mem_pool, const CppType& value) {
        auto it = bucket_map.find(value);
        if (it != bucket_map.end()) {
            return it;
        }
        if constexpr (IsSlice<CppType>) {
            uint8_t* pos = mem_pool->allocate(value.size);
            std::memcpy(pos, value.data, value.size);
            it = bucket_map.emplace(Slice{pos, value.size}, 0).first;
        } else {
            it = bucket_map.emplace(value, 0).first;
        }
        return it;
    }

    void update(FunctionContext* ctx, const Column* column, size_t row_num) {
        auto it = bucket_map.upper_bound(column->get(row_num).get<RunTimeCppType<LT>>());
        if (it != bucket_map.end()) {
            it->second++;
        } else {
            last_bucket_counter++;
        }
    }

    // Returns the total size in bytes required to encode this object.
    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(uint32_t); // size of bucket_map
        if constexpr (IsSlice<CppType>) {
            result += sizeof(uint32_t) * bucket_map.size(); // sizes of keys
            for (const auto& [key, counter] : bucket_map) {
                result += key.size;
            }
        } else {
            result += sizeof(CppType) * bucket_map.size(); // keys of bucket_map
        }
        result += sizeof(CounterType) * bucket_map.size(); // counters of bucket_map
        result += sizeof(CounterType);                     // last_bucket_counter
        result += sizeof(uint8_t) * 2;                     // no_lower_bound, no_upper_bound
        return result;
    }

    // Writes and binary encoded version of the object to dst.
    // The size written will be serialized_size()
    // As of 2023-12-20, SR drops array literal in merge and _const_columns in merge is not aligned with
    // _arg_types. So we pass all consts from update() through serialization.
    void serialize(uint8_t* dst) const {
        uint32_t bucket_map_size = bucket_map.size();
        memcpy(dst, &bucket_map_size, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        for (const auto& [key, counter] : bucket_map) {
            if constexpr (IsSlice<CppType>) {
                uint32_t size = key.size;
                memcpy(dst, &size, sizeof(uint32_t));
                dst += sizeof(uint32_t);
                memcpy(dst, key.data, key.size);
                dst += key.size;
            } else {
                memcpy(dst, &key, sizeof(CppType));
                dst += sizeof(CppType);
            }
            memcpy(dst, &counter, sizeof(CounterType));
            dst += sizeof(CounterType);
        }
        memcpy(dst, &last_bucket_counter, sizeof(CounterType));
        dst += sizeof(CounterType);
        uint8_t nlb = no_lower_bound;
        memcpy(dst, &nlb, sizeof(uint8_t));
        dst += sizeof(uint8_t);
        uint8_t nub = no_upper_bound;
        memcpy(dst, &nub, sizeof(uint8_t));
        dst += sizeof(uint8_t);
    }

    // Deserializes a CelonisHistogramBoundariesAggregateState object and merges it with the current state.
    void deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) {
        const uint8_t* end = src + len;

        uint32_t bucket_map_size;
        memcpy(&bucket_map_size, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        for (auto i = 0; i < bucket_map_size; ++i) {
            if constexpr (IsSlice<CppType>) {
                uint32_t size;
                memcpy(&size, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                Slice key{src, size};
                src += size;
                CounterType counter;
                memcpy(&counter, src, sizeof(CounterType));
                src += sizeof(CounterType);
                auto it = may_insert_boundary(mem_pool, key);
                it->second += counter;
            } else {
                CppType key;
                memcpy(&key, src, sizeof(CppType));
                src += sizeof(CppType);
                CounterType counter;
                memcpy(&counter, src, sizeof(CounterType));
                src += sizeof(CounterType);
                auto it = may_insert_boundary(mem_pool, key);
                it->second += counter;
            }
        }
        CounterType last_counter;
        memcpy(&last_counter, src, sizeof(CounterType));
        src += sizeof(CounterType);
        last_bucket_counter += last_counter;
        uint8_t nlb;
        memcpy(&nlb, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        no_lower_bound = nlb;
        uint8_t nub;
        memcpy(&nub, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        no_upper_bound = nub;
        DCHECK_EQ(src, end);
        initialized = true;
    }

    bool initialized = false;
    bool no_lower_bound = false;
    bool no_upper_bound = false;
    BucketMap bucket_map;
    CounterType last_bucket_counter = 0;
};

/**
 * @param: [col, no_lower_bound, no_upper_bound, boundaries]
 * @paramType: [BIGINT | DOUBLE | DATETIME | VARCHAR, CONST BOOLEAN, CONST BOOLEAN, ARRAY of col type]
 * @return: STRUCT {
 *      class_bounds_lower: ARRAY of col type
 *      class_bounds_upper: ARRAY of col type
 *      class_count: ARRAY_BIGINT
 *    }
 * Note: Returns NULL when there is no valid bucket with the give arguments, e.g., false, false, [].
 *       Saola implementation falls back to BUCKET_COUNT=10 instead.
 *
 * Implements PQL HISTOGRAM with mode BOUNDARIES
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245736/HISTOGRAM
 */
template <LogicalType LT, typename T = RunTimeCppType<LT>>
class CelonisHistogramBoundariesAggregationFunction final
        : public AggregateFunctionBatchHelper<CelonisHistogramBoundariesAggregateState<LT>,
                                              CelonisHistogramBoundariesAggregationFunction<LT, T>> {
public:
    using ColumnType = RunTimeColumnType<LT>;

    void create_impl(FunctionContext* ctx, const Column** columns,
                     CelonisHistogramBoundariesAggregateState<LT>& state) const {
        DCHECK_EQ(ctx->get_num_args(), 4);
        state.initialized = true;
        if (ctx->is_notnull_constant_column(1)) {
            state.no_lower_bound = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(1));
        }
        if (ctx->is_notnull_constant_column(2)) {
            state.no_upper_bound = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(2));
        }
        const auto* boundary_column = ctx->get_constant_column(3).get();
        if (boundary_column == nullptr) {
            boundary_column = columns[3];
        }
        if (boundary_column->is_null(0)) {
            return;
        }
        UnnestedArrayData boundaries_array_data = prepare_array_input(boundary_column);
        const auto& elements = down_cast<const ColumnType*>(boundaries_array_data.elements)->get_data().data();
        auto start = boundaries_array_data.offsets->get(0).get_uint32();
        auto end = boundaries_array_data.offsets->get(1).get_uint32();
        for (auto i = start; i < end; ++i) {
            if (boundaries_array_data.null_elements != nullptr && (*boundaries_array_data.null_elements)[i]) {
                continue;
            }
            state.may_insert_boundary(ctx->mem_pool(), elements[i]);
        }
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        auto& state_impl = this->data(state);
        if (!state_impl.initialized) {
            create_impl(ctx, columns, state_impl);
        }
        if (columns[0]->is_nullable() && columns[0]->is_null(row_num)) {
            return;
        }
        this->data(state).update(ctx, columns[0], row_num);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        // merge internal state with column[row_num]
        // the column type is binary
        if (column->is_nullable() && column->is_null(row_num)) {
            return;
        }
        const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
        Slice slice = input_column->get_slice(row_num);
        this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*)slice.data, slice.size);
    }

    bool support_nullable_immediate_input() const override { return true; }

    void serialize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                             Column* to) const override {
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

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        // Used for streaming aggregation passthrough. Not implemented.
        throw std::runtime_error("celonis_histogram_boundaries: convert_to_serialize_format not supported");
    }

    void finalize_to_column(FunctionContext* ctx __attribute__((unused)), ConstAggDataPtr __restrict state,
                            Column* to) const override {
        auto& state_impl = this->data(state);
        if (!state_impl.initialized) {
            to->append_default();
            return;
        }
        DatumArray class_bounds_lower;
        DatumArray class_bounds_upper;
        DatumArray class_count;
        auto prev_it = state_impl.bucket_map.cend();
        for (auto it = state_impl.bucket_map.cbegin(); it != state_impl.bucket_map.cend(); prev_it = it++) {
            if (prev_it == state_impl.bucket_map.cend()) {
                if (!state_impl.no_lower_bound) {
                    continue;
                }
                class_bounds_lower.push_back(kNullDatum);
            } else {
                class_bounds_lower.emplace_back(prev_it->first);
            }
            class_bounds_upper.emplace_back(it->first);
            class_count.emplace_back(it->second);
        }
        if (state_impl.no_upper_bound) {
            auto reverse_it = state_impl.bucket_map.crbegin();
            if (reverse_it != state_impl.bucket_map.crend()) {
                class_bounds_lower.emplace_back(state_impl.bucket_map.crbegin()->first);
            } else {
                class_bounds_lower.push_back(kNullDatum);
            }
            class_bounds_upper.push_back(kNullDatum);
            class_count.emplace_back(state_impl.last_bucket_counter);
        }
        if (class_bounds_lower.empty()) {
            to->append_default();
            return;
        }
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        auto* struct_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(to));
        struct_column->field_column("class_bounds_lower")->append_datum(class_bounds_lower);
        struct_column->field_column("class_bounds_upper")->append_datum(class_bounds_upper);
        struct_column->field_column("class_count")->append_datum(class_count);
    }

    std::string get_name() const override { return "celonis_histogram_boundaries"; }
};

} // namespace starrocks
