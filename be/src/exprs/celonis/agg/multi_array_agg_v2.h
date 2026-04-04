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
#include "exprs/function_context.h"
#include "exprs/function_helper.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

// Arena-allocated linked list node. Each node holds exactly one serialized row.
// Allocated from FunctionContext::mem_pool(), freed in bulk via MemPool::free_all().
struct ArenaNode {
    ArenaNode* next;
    char data[0]; // flexible array member — one serialized row
};

// Per-group state: linked list of arena-allocated row nodes.
struct MultiArrayAggV2AggregateState {
    ArenaNode* head = nullptr;
};

// MULTI_ARRAY_AGG V2: arena-allocated linked list, one row per node.
//
// Serialization format:
//   Row format: [null_bitmap (ceil(N/8) bytes)][non-null field values...]
//   Dict-encoded fields use compact fixed-width encoding (1/2/3 bytes per value).
//   Non-dict fields use Column::serialize().
//
// Intermediate type: VARBINARY blob per group.
//
// per-group storage uses a linked list of arena-allocated nodes
class MultiArrayAggV2AggregateFunction final
        : public AggregateFunctionBatchHelper<MultiArrayAggV2AggregateState, MultiArrayAggV2AggregateFunction> {
public:
    void create(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        new (ptr) MultiArrayAggV2AggregateState;
    }

    void destroy(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        // No subtract: lets peak_agg_state_memory_usage() reflect true peak.
        // Arena nodes freed in bulk by MemPool::free_all().
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

        // Allocate node from arena
        auto* node = reinterpret_cast<ArenaNode*>(ctx->mem_pool()->allocate_aligned(sizeof(ArenaNode) + row_size, 8));
        node->next = s.head;

        uint8_t* bitmap = reinterpret_cast<uint8_t*>(node->data);
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

        s.head = node;
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

        size_t num_fields = ctx->get_arg_types().size();
        size_t nbm = (num_fields + 7) / 8;

        // Split blob into individual row nodes
        const uint8_t* pos = reinterpret_cast<const uint8_t*>(slice.data);
        const uint8_t* end = pos + slice.size;
        while (pos < end) {
            size_t row_size = _compute_row_size(ctx, pos, num_fields, nbm);
            if (pos + row_size > end) {
                ctx->set_error("multi_array_agg_v2: corrupted serialized data in merge", false);
                return;
            }
            auto* node =
                    reinterpret_cast<ArenaNode*>(ctx->mem_pool()->allocate_aligned(sizeof(ArenaNode) + row_size, 8));
            node->next = s.head;
            memcpy(node->data, pos, row_size);

            s.head = node;
            pos += row_size;
        }
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& s = this->data(const_cast<AggDataPtr>(state));

        size_t num_fields = ctx->get_arg_types().size();
        size_t nbm = (num_fields + 7) / 8;

        // Compute total data size by traversing the linked list
        size_t total_size = 0;
        for (ArenaNode* n = s.head; n; n = n->next) {
            total_size += _compute_row_size(ctx, reinterpret_cast<const uint8_t*>(n->data), num_fields, nbm);
        }

        auto* bin = ColumnHelper::get_binary_column(to);
        auto& bytes = bin->get_bytes();
        size_t old_size = bytes.size();
        bytes.resize(old_size + total_size);
        uint8_t* dst = bytes.data() + old_size;

        for (ArenaNode* n = s.head; n; n = n->next) {
            size_t row_size = _compute_row_size(ctx, reinterpret_cast<const uint8_t*>(n->data), num_fields, nbm);
            memcpy(dst, n->data, row_size);
            dst += row_size;
        }

        bin->get_offset().push_back(bytes.size());
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& s = this->data(const_cast<AggDataPtr>(state));
        size_t num_fields = ctx->get_arg_types().size();
        size_t nbm = (num_fields + 7) / 8;
        size_t num_order_by = ctx->get_is_asc_order().size();
        size_t num_agg_cols = num_fields - num_order_by;

        // Compute total data size by traversing the linked list
        size_t total_size = 0;
        for (ArenaNode* n = s.head; n; n = n->next) {
            total_size += _compute_row_size(ctx, reinterpret_cast<const uint8_t*>(n->data), num_fields, nbm);
        }

        // Flatten linked list into contiguous buffer for _deserialize_buffer()
        std::string flat;
        flat.resize(total_size);
        char* dst = flat.data();
        for (ArenaNode* n = s.head; n; n = n->next) {
            size_t row_size = _compute_row_size(ctx, reinterpret_cast<const uint8_t*>(n->data), num_fields, nbm);
            memcpy(dst, n->data, row_size);
            dst += row_size;
        }

        // Deserialize rows from buffer
        MutableColumns tmp = _deserialize_buffer(ctx, flat, num_fields, nbm);
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
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        auto* bin = ColumnHelper::get_binary_column(dst->get());
        auto& bytes = bin->get_bytes();
        auto& offsets = bin->get_offset();

        size_t num_fields = src.size();
        size_t nbm = (num_fields + 7) / 8;

        for (size_t row = 0; row < chunk_size; ++row) {
            uint32_t row_size = static_cast<uint32_t>(nbm);
            for (size_t i = 0; i < num_fields; ++i) {
                if (!_is_null(src[i].get(), row)) {
                    auto [dc, ar] = _unwrap(src[i].get(), row);
                    int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                    row_size += serialization_size == 0 ? dc->serialize_size(ar) : serialization_size;
                }
            }

            size_t old_size = bytes.size();
            bytes.resize(old_size + row_size);
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
        }

        if (dst->get()->is_nullable()) {
            for (size_t i = 0; i < chunk_size; i++) {
                down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
            }
        }
    }

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& s = this->data(state);
        s.head = nullptr;
        // Arena nodes leaked until MemPool::free_all() — bounded by streaming batch size
    }

    std::string get_name() const override { return "multi_array_agg_v2"; }

private:
    // Compute the byte size of a serialized row by scanning its null bitmap and fields.
    // Does not deserialize — just advances a pointer past each field.
    static size_t _compute_row_size(FunctionContext* ctx, const uint8_t* data, size_t num_fields, size_t nbm) {
        const uint8_t* bitmap = data;
        const uint8_t* pos = data + nbm;

        for (size_t i = 0; i < num_fields; ++i) {
            bool is_null = (bitmap[i / 8] >> (i % 8)) & 1;
            if (!is_null) {
                int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                if (serialization_size > 0) {
                    pos += serialization_size;
                } else {
                    pos += _serialized_field_size(pos, ctx->get_arg_type(i)->type);
                }
            }
        }
        return static_cast<size_t>(pos - data);
    }

    // Determine byte size of a non-dict serialized field from the serialized bytes.
    // Fixed-length types: sizeof(T). Variable-length types: [uint32_t len][data].
    static size_t _serialized_field_size(const uint8_t* pos, LogicalType type) {
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
            uint32_t len;
            memcpy(&len, pos, sizeof(uint32_t));
            return sizeof(uint32_t) + len;
        }
        }
    }

    static MutableColumns _deserialize_buffer(FunctionContext* ctx, const std::string& buffer, size_t num_fields,
                                              size_t nbm) {
        MutableColumns cols;
        cols.reserve(num_fields);
        for (size_t i = 0; i < num_fields; ++i) {
            cols.emplace_back(ColumnHelper::create_column(*ctx->get_arg_type(i), true));
        }

        const uint8_t* pos = reinterpret_cast<const uint8_t*>(buffer.data());
        const uint8_t* end = pos + buffer.size();
        while (pos < end) {
            const uint8_t* bitmap = pos;
            pos += nbm;

            for (size_t i = 0; i < num_fields; ++i) {
                bool is_null = (bitmap[i / 8] >> (i % 8)) & 1;
                auto* nullable = down_cast<NullableColumn*>(cols[i].get());
                if (is_null) {
                    nullable->append_nulls(1);
                } else {
                    nullable->null_column_data().emplace_back(0);
                    int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
                    if (serialization_size != 0) {
                        int32_t value = _deserialize_fixed_length(pos, serialization_size);
                        static_cast<Int32Column*>(nullable->data_column().get())->append(value);
                        pos += serialization_size;
                    } else {
                        pos = nullable->data_column()->deserialize_and_append(pos);
                    }
                }
            }
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
