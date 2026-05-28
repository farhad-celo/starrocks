// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstring>

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exec/sorting/sorting.h"
#include "exprs/agg/aggregate.h"
#include "exprs/agg/aggregate_state_allocator.h"
#include "exprs/function_context.h"
#include "exprs/function_helper.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"
#include "util/raw_container.h"

namespace starrocks {

// Per-group state: a single contiguous vector of bytes holding all serialized rows.
struct MultiArrayAggV2AggregateState {
    using ContainerT = raw::RawVector<uint8_t, AggregateStateAllocator<uint8_t>>;
    ContainerT buffer;
};

// MULTI_ARRAY_AGG V2: contiguous byte vector allocation, one row packed after another.
//
// Serialization format:
//   Row format: [null_bitmap (ceil(N/8) bytes)][non-null field values...]
//   Dict-encoded fields use compact fixed-width encoding (1/2/3 bytes per value).
//   Non-dict fields use Column::serialize().
//
// Intermediate type: a VARBINARY blob per group.
class MultiArrayAggV2AggregateFunction final
        : public AggregateFunctionBatchHelper<MultiArrayAggV2AggregateState, MultiArrayAggV2AggregateFunction> {
public:
    void create(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        new (ptr) MultiArrayAggV2AggregateState;
    }

    void destroy(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        this->data(ptr).~MultiArrayAggV2AggregateState();
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        auto& s = this->data(state);
        size_t num_fields = ctx->get_arg_types().size();
        size_t nbm = (num_fields + 7) / 8;

        // Compute serialized row size
        size_t row_size = nbm;
        for (size_t i = 0; i < num_fields; ++i) {
            if (!_is_null(columns[i], row_num)) {
                auto [dc, ar] = _unwrap(columns[i], row_num);
                int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                row_size +=
                        (serialization_size == 0) ? dc->serialize_size(ar) : static_cast<size_t>(serialization_size);
            }
        }

        size_t old_size = s.buffer.size();
        s.buffer.resize(old_size + row_size);
        uint8_t* dst = s.buffer.data() + old_size;

        uint8_t* bitmap = dst;
        memset(bitmap, 0, nbm);
        uint8_t* pos = bitmap + nbm;

        for (size_t i = 0; i < num_fields; ++i) {
            if (_is_null(columns[i], row_num)) {
                bitmap[i / 8] |= (1 << (i % 8));
            } else {
                auto [dc, ar] = _unwrap(columns[i], row_num);
                int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                if (serialization_size == 0) {
                    pos += dc->serialize(ar, pos);
                } else {
                    // This case only happens for dictified columns which have INT32 type.
                    _serialize_fixed_length(static_cast<const Int32Column*>(dc)->get_data()[ar], pos,
                                            serialization_size);
                    pos += serialization_size;
                }
            }
        }
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        auto& s = this->data(state);

        Slice slice;
        if (column->is_nullable()) {
            if (column->is_null(row_num)) return;
            auto* data_col = down_cast<const NullableColumn*>(column)->data_column().get();
            slice = down_cast<const BinaryColumn*>(data_col)->get_slice(row_num);
        } else {
            slice = down_cast<const BinaryColumn*>(column)->get_slice(row_num);
        }

        if (slice.size == 0) return;

        if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
            // Perform validation on incoming buffer structure before appending
            size_t num_fields = ctx->get_arg_types().size();
            size_t nbm = (num_fields + 7) / 8;
            const uint8_t* pos = reinterpret_cast<const uint8_t*>(slice.data);
            const uint8_t* end = pos + slice.size;
            while (pos < end) {
                size_t row_size = _compute_row_size(ctx, pos, num_fields, nbm, end);
                if (end - pos < row_size) {
                    _emit_corruption_error(ctx, "merge", slice, pos, row_size, num_fields, nbm);
                    return;
                }
                pos += row_size;
            }
        }

        const size_t old_size = s.buffer.size();
        s.buffer.resize(old_size + slice.size);
        uint8_t* dst_ptr = s.buffer.data() + old_size;
        memcpy(dst_ptr, slice.data, slice.size);
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& s = this->data(const_cast<AggDataPtr>(state));

        auto* bin = ColumnHelper::get_binary_column(to);
        if (UNLIKELY(_would_overflow_intermediate(ctx, bin, s.buffer.size(), "serialize_to_column"))) return;

        auto& bytes = bin->get_bytes();
        const size_t old_size = bytes.size();
        bytes.resize(bytes.size() + s.buffer.size());
        uint8_t* dst_ptr = bytes.data() + old_size;
        memcpy(dst_ptr, s.buffer.data(), s.buffer.size());

        bin->get_offset().push_back(bytes.size());
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
            size_t num_fields = ctx->get_arg_types().size();
            size_t nbm = (num_fields + 7) / 8;
            _verify_slice_roundtrip(ctx, bin, bin->get_offset().size() - 2, num_fields, nbm, "serialize_to_column");
        }
        s.buffer = MultiArrayAggV2AggregateState::ContainerT();
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& s = this->data(const_cast<AggDataPtr>(state));
        size_t num_fields = ctx->get_arg_types().size();
        size_t nbm = (num_fields + 7) / 8;
        size_t num_order_by = ctx->get_is_asc_order().size();
        size_t num_agg_cols = num_fields - num_order_by;

        MutableColumns tmp = _deserialize_buffer(ctx, s.buffer, num_fields, nbm);
        uint32_t count = tmp.empty() ? 0 : static_cast<uint32_t>(tmp[0]->size());

        // Apply ORDER BY sorting
        Buffer<uint32_t> index;
        if (num_order_by > 0 && count > 0) {
            Columns order_by_columns;
            SortDescs sort_desc(ctx->get_is_asc_order(), ctx->get_nulls_first());
            order_by_columns.assign(tmp.begin() + num_agg_cols, tmp.end());
            Permutation perm;
            Status st = sort_and_tie_columns(ctx->state()->cancelled_ref(), order_by_columns, sort_desc, &perm);
            order_by_columns.clear();
            if (UNLIKELY(ctx->state()->cancelled_ref())) {
                ctx->set_error("multi_array_agg detects cancelled.", false);
                to->append_default();
                return;
            }
            if (UNLIKELY(!st.ok())) {
                ctx->set_error(st.to_string().c_str(), false);
                to->append_default();
                return;
            }
            if (!perm.empty()) {
                index.resize(count);
                for (uint32_t i = 0; i < count; ++i) {
                    index[i] = perm[i].index_in_chunk;
                }
            }
        }

        auto* struct_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(to));
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }

        for (size_t i = 0; i < num_agg_cols; ++i) {
            auto* field_column = struct_column->fields_column()[i].get();
            auto* array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(field_column));
            if (field_column->is_nullable()) {
                down_cast<NullableColumn*>(field_column)->null_column_data().emplace_back(0);
            }
            auto* elements_col = array_col->elements_column().get();
            auto* offsets_col = array_col->offsets_column().get();
            if (index.empty()) {
                if (count > 0) {
                    elements_col->append(*tmp[i], 0, count);
                }
            } else {
                elements_col->append_selective(*tmp[i], index);
            }
            offsets_col->append(offsets_col->get_data().back() + (index.empty() ? count : index.size()));
        }

        if (UNLIKELY(_check_overflow(*to, ctx))) {
            return;
        }
        s.buffer = MultiArrayAggV2AggregateState::ContainerT();
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        auto* bin = ColumnHelper::get_binary_column(dst->get());
        auto& bytes = bin->get_bytes();
        auto& offsets = bin->get_offset();

        size_t num_fields = src.size();
        size_t nbm = (num_fields + 7) / 8;

        for (size_t row = 0; row < chunk_size; ++row) {
            size_t row_size = nbm;
            for (size_t i = 0; i < num_fields; ++i) {
                if (!_is_null(src[i].get(), row)) {
                    auto [dc, ar] = _unwrap(src[i].get(), row);
                    int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                    row_size +=
                            serialization_size == 0 ? dc->serialize_size(ar) : static_cast<size_t>(serialization_size);
                }
            }
            if (UNLIKELY(_would_overflow_intermediate(ctx, bin, row_size, "convert_to_serialize_format"))) return;

            const size_t old_size = bytes.size();
            bytes.resize(bytes.size() + row_size);
            uint8_t* dst_ptr = bytes.data() + old_size;

            memset(dst_ptr, 0, nbm);
            uint8_t* bitmap = dst_ptr;
            dst_ptr += nbm;

            for (size_t i = 0; i < num_fields; ++i) {
                if (_is_null(src[i].get(), row)) {
                    bitmap[i / 8] |= (1 << (i % 8));
                } else {
                    int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                    auto [dc, ar] = _unwrap(src[i].get(), row);
                    if (serialization_size == 0) {
                        dst_ptr += dc->serialize(ar, dst_ptr);
                    } else {
                        _serialize_fixed_length(dc->get(ar).get_int32(), dst_ptr, serialization_size);
                        dst_ptr += serialization_size;
                    }
                }
            }

            offsets.push_back(bytes.size());
            if (dst->get()->is_nullable()) {
                down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
            }
            if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
                if (_verify_slice_roundtrip(ctx, bin, offsets.size() - 2, num_fields, nbm,
                                            "convert_to_serialize_format"))
                    return;
            }
        }
    }

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& s = this->data(state);
        s.buffer = MultiArrayAggV2AggregateState::ContainerT();
    }

    std::string get_name() const override { return "multi_array_agg_v2"; }

private:
    // Pre-flight check that the intermediate BinaryColumn won't overflow uint32_t offsets.
    static bool _would_overflow_intermediate(FunctionContext* ctx, const BinaryColumn* bin, size_t add,
                                             const char* origin) {
        size_t cur = bin->get_bytes().size();
        size_t limit = static_cast<size_t>(Column::MAX_CAPACITY_LIMIT);
        if (add >= limit || cur + add >= limit) {
            ctx->set_error(
                    fmt::format(
                            "multi_array_agg_v2: intermediate VARBINARY exceeds 4 GiB in {} (current={}, adding={})",
                            origin, cur, add)
                            .c_str(),
                    false);
            return true;
        }
        return false;
    }

    // Debug: render up to max bytes of slice as comma-separated decimal codes.
    static std::string _slice_bytes_as_codes(const Slice& slice, size_t max_bytes) {
        size_t dump = std::min<size_t>(max_bytes, slice.size);
        std::string out;
        auto* data = reinterpret_cast<const uint8_t*>(slice.data);
        for (size_t i = 0; i < dump; ++i) {
            if (i != 0) out.push_back(',');
            out += std::to_string(static_cast<unsigned>(data[i]));
        }
        return out;
    }

    // Emit "corrupted serialized data" error; verbose when debug_level > 0.
    static void _emit_corruption_error(FunctionContext* ctx, const char* origin, const Slice& slice, const uint8_t* pos,
                                       size_t row_size, size_t num_fields, size_t nbm) {
        if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
            size_t off = static_cast<size_t>(pos - reinterpret_cast<const uint8_t*>(slice.data));
            ctx->set_error(fmt::format("multi_array_agg_v2: corrupted serialized data in {} (num_fields={}, nbm={}, "
                                       "slice.size={}, pos_offset={}, row_size={}, pos+row_size={}, bytes=[{}])",
                                       origin, num_fields, nbm, slice.size, off, row_size, off + row_size,
                                       _slice_bytes_as_codes(slice, 500))
                                   .c_str(),
                           false);
        } else {
            ctx->set_error(fmt::format("multi_array_agg_v2: corrupted serialized data in {}", origin).c_str(), false);
        }
    }

    // Debug: walk the row just appended via bin->get_slice() — same path merge() uses.
    static bool _verify_slice_roundtrip(FunctionContext* ctx, const BinaryColumn* bin, size_t row_idx,
                                        size_t num_fields, size_t nbm, const char* origin) {
        Slice slice = bin->get_slice(row_idx);
        const uint8_t* pos = reinterpret_cast<const uint8_t*>(slice.data);
        const uint8_t* end = pos + slice.size;
        while (pos < end) {
            size_t row_size = _compute_row_size(ctx, pos, num_fields, nbm, end);
            if (end - pos < row_size) {
                _emit_corruption_error(ctx, origin, slice, pos, row_size, num_fields, nbm);
                return true;
            }
            pos += row_size;
        }
        return false;
    }

    // Compute the byte size of a serialized row by scanning its null bitmap and fields.
    // Does not deserialize — just advances a pointer past each field.
    // This function is for only for debug mode.
    static size_t _compute_row_size(FunctionContext* ctx, const uint8_t* data, size_t num_fields, size_t nbm,
                                    const uint8_t* end) {
        if (end - data < nbm) {
            return (end - data) + 1;
        }
        const uint8_t* bitmap = data;
        const uint8_t* pos = data + nbm;

        for (size_t i = 0; i < num_fields; ++i) {
            bool is_null = (bitmap[i / 8] >> (i % 8)) & 1;
            if (!is_null) {
                int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                if (serialization_size > 0) {
                    pos += serialization_size;
                } else {
                    pos += _serialized_field_size(pos, ctx->get_arg_type(i)->type, end);
                }
            }
        }
        return static_cast<size_t>(pos - data);
    }

    // Determine byte size of a non-dict serialized field from the serialized bytes.
    // Fixed-length types: sizeof(T). Variable-length types: [uint32_t len][data].
    static size_t _serialized_field_size(const uint8_t* pos, LogicalType type, const uint8_t* end) {
        switch (type) {
        case TYPE_BOOLEAN:
        case TYPE_TINYINT:
            return 1;
        case TYPE_SMALLINT:
            return 2;
        case TYPE_INT:
        case TYPE_FLOAT:
        case TYPE_DATE:
        case TYPE_DECIMAL32:
            return 4;
        case TYPE_BIGINT:
        case TYPE_DOUBLE:
        case TYPE_DATETIME:
        case TYPE_DECIMAL64:
            return 8;
        case TYPE_LARGEINT:
        case TYPE_DECIMALV2:
        case TYPE_DECIMAL128:
            return 16;
        case TYPE_VARCHAR:
        case TYPE_CHAR:
        case TYPE_VARBINARY:
        case TYPE_HLL:
        case TYPE_OBJECT:
        case TYPE_JSON:
        default: {
            // All non-fixed-width types use [uint32_t len][data] serialization format.
            if (UNLIKELY(end - pos < sizeof(uint32_t))) {
                return end - pos + 1;
            }
            uint32_t len;
            memcpy(&len, pos, sizeof(uint32_t));
            return sizeof(uint32_t) + len;
        }
        }
    }

    static MutableColumns _deserialize_buffer(FunctionContext* ctx,
                                              const MultiArrayAggV2AggregateState::ContainerT& buffer,
                                              size_t num_fields, size_t nbm) {
        auto make_cols = [&]() {
            MutableColumns cols;
            cols.reserve(num_fields);
            for (size_t i = 0; i < num_fields; ++i) {
                cols.emplace_back(ColumnHelper::create_column(*ctx->get_arg_type(i), true));
            }
            return cols;
        };
        MutableColumns cols = make_cols();
        if (buffer.empty()) {
            return cols;
        }
        const uint8_t* pos = reinterpret_cast<const uint8_t*>(buffer.data());
        const uint8_t* end = pos + buffer.size();
        while (pos < end) {
            if (UNLIKELY(end - pos < nbm)) {
                _emit_corruption_error(ctx, "finalize", {buffer.data(), buffer.size()}, pos, 0, num_fields, nbm);
                return make_cols();
            }
            const uint8_t* bitmap = pos;
            pos += nbm;

            for (size_t i = 0; i < num_fields; ++i) {
                bool is_null = (bitmap[i / 8] >> (i % 8)) & 1;
                auto* nullable = down_cast<NullableColumn*>(cols[i].get());
                if (is_null) {
                    nullable->append_nulls(1);
                } else {
                    int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                    if (serialization_size != 0) {
                        if (UNLIKELY(end - pos < serialization_size)) {
                            _emit_corruption_error(ctx, "finalize", {buffer.data(), buffer.size()}, pos, 0, num_fields,
                                                   nbm);
                            return make_cols();
                        }
                        nullable->null_column_data().emplace_back(0);
                        int32_t value = _deserialize_fixed_length(pos, serialization_size);
                        static_cast<Int32Column*>(nullable->data_column().get())->append(value);
                        pos += serialization_size;
                    } else {
                        size_t field_size = _serialized_field_size(pos, ctx->get_arg_type(i)->type, end);
                        if (UNLIKELY(end - pos < field_size)) {
                            _emit_corruption_error(ctx, "finalize", {buffer.data(), buffer.size()}, pos, 0, num_fields,
                                                   nbm);
                            return make_cols();
                        }
                        nullable->null_column_data().emplace_back(0);
                        pos = nullable->data_column()->deserialize_and_append(pos);
                    }
                }
            }
        }
        if (pos != end) {
            _emit_corruption_error(ctx, "finalize", {buffer.data(), buffer.size()}, pos, 0, num_fields, nbm);
            return make_cols();
        }
        return cols;
    }

    static bool _is_null(const Column* col, size_t row_num) {
        return (col->is_nullable() && col->is_null(row_num)) || col->only_null();
    }

    static std::pair<const Column*, size_t> _unwrap(const Column* col, size_t row_num) {
        if (col->is_constant()) {
            col = down_cast<const ConstColumn*>(col)->data_column().get();
            row_num = 0;
        }
        if (col->is_nullable()) {
            col = down_cast<const NullableColumn*>(col)->data_column().get();
        }
        return {col, row_num};
    }

    static bool _check_overflow(const Column& col, FunctionContext* ctx) {
        Status st = col.capacity_limit_reached();
        if (!st.ok()) {
            ctx->set_error(
                    fmt::format("The column generated by multi_array_agg is overflow: {}", st.message()).c_str());
            return true;
        }
        return false;
    }

    static void _serialize_fixed_length(int value, uint8_t* buffer, int serialization_size) {
        for (int i = 0; i < serialization_size; ++i) {
            *buffer++ = value & 255;
            value >>= 8;
        }
    }

    static int _deserialize_fixed_length(const uint8_t* buffer, int serialization_size) {
        int result = 0;
        for (int i = 0; i < serialization_size; ++i) {
            result |= static_cast<uint32_t>(*buffer++) << (8 * i);
        }
        return result;
    }
};

} // namespace starrocks
