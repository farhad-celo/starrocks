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
#include <limits>

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
#include "util/raw_container.h"
#include "util/uid_util.h"

namespace starrocks {

// The row bytes follow this header in the same MemPool block, so only the length is stored.
// A node holds whole rows -- one for update(), the entire payload for merge() -- so rows never
// straddle a node and finalize can parse each one on its own.
// size is 32-bit; widening it would not change sizeof, since `next` pads the struct to 16 anyway.
struct MultiArrayAggV3ListNode {
    MultiArrayAggV3ListNode* next = nullptr;
    uint32_t size = 0;

    const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(this) + sizeof(MultiArrayAggV3ListNode); }
};

// Nodes are front-inserted, so traversal yields reverse-insertion order. That is fine: ORDER BY
// sorts in finalize, and without it the element order was never guaranteed.
struct MultiArrayAggV3AggregateState {
    MultiArrayAggV3ListNode* head = nullptr;
    uint64_t num_rows = 0;
    // Sum of the node sizes, so serialize_to_column does not have to walk the list to size itself.
    uint64_t total_bytes = 0;
};

// MULTI_ARRAY_AGG V3: rows live in a linked list of MemPool blocks instead of V2's single
// contiguous buffer, so a growing group never reallocates and copies.
//
// Wire format, byte-identical to V2 -- one VARBINARY blob per group:
//   [uint64_t row_count][rows...], row = [null_bitmap ceil(N/8)][non-null field values]
//   Dict-encoded fields use a fixed 1-3 byte encoding, everything else Column::serialize().
//
// MemPool has no per-allocation free, so dropping the list only drops pointers; the bytes come
// back when the pool is cleared at the end of the operator's life.
class MultiArrayAggV3AggregateFunction final
        : public AggregateFunctionBatchHelper<MultiArrayAggV3AggregateState, MultiArrayAggV3AggregateFunction> {
public:
    void create(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        new (ptr) MultiArrayAggV3AggregateState;
    }

    void destroy(FunctionContext* ctx, AggDataPtr __restrict ptr) const override {
        this->data(ptr).~MultiArrayAggV3AggregateState();
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        auto& s = this->data(state);
        if (UNLIKELY(_check_size_limit(ctx, s.num_rows, 1))) {
            return;
        }
        const size_t num_fields = ctx->get_arg_types().size();
        const size_t nbm = _null_bitmap_bytes(num_fields);
        const auto& ser_sizes = ctx->get_multi_array_agg_column_serialization_size();

        size_t row_size = nbm;
        for (size_t i = 0; i < num_fields; ++i) {
            row_size += _field_size(columns[i], row_num, ser_sizes[i]);
        }

        uint8_t* dst = _alloc_row_node(ctx, s, row_size);
        if (UNLIKELY(dst == nullptr)) {
            return;
        }

        uint8_t* bitmap = dst;
        memset(bitmap, 0, nbm);
        uint8_t* pos = bitmap + nbm;
        for (size_t i = 0; i < num_fields; ++i) {
            _write_field(bitmap, pos, i, columns[i], row_num, ser_sizes[i]);
        }

        s.num_rows += 1;
        s.total_bytes += row_size;
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

        uint64_t added_rows;
        if (UNLIKELY(!_read_row_count_prefix(ctx, slice, "merge", &added_rows))) {
            return;
        }

        // Check the prospective total without committing it; num_rows is advanced at the bottom,
        // once the payload is actually stored.
        if (UNLIKELY(_check_size_limit(ctx, s.num_rows, added_rows))) {
            return;
        }

        if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
            const size_t num_fields = ctx->get_arg_types().size();
            if (_validate_serialized_slice(ctx, slice, num_fields, _null_bitmap_bytes(num_fields), "merge")) {
                return;
            }
        }

        const size_t payload_size = slice.size - kRowCountPrefixBytes;
        if (payload_size == 0) {
            // added_rows is 0 here for any well-formed blob; if it is not, the blob is corrupt and
            // dropping the count is what keeps num_rows honest.
            return;
        }
        uint8_t* buf = _alloc_row_node(ctx, s, payload_size);
        if (UNLIKELY(buf == nullptr)) {
            return;
        }
        memcpy(buf, slice.data + kRowCountPrefixBytes, payload_size);
        s.num_rows += added_rows;
        s.total_bytes += payload_size;
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& s = this->data(const_cast<AggDataPtr>(state));

        const size_t total = s.total_bytes;

        auto* bin = ColumnHelper::get_binary_column(to);
        if (UNLIKELY(_would_overflow_intermediate(ctx, bin, kRowCountPrefixBytes + total, "serialize_to_column"))) {
            return;
        }

        auto& bytes = bin->get_bytes();
        const size_t old_size = bytes.size();
        bytes.resize(old_size + kRowCountPrefixBytes + total);
        uint8_t* dst_ptr = bytes.data() + old_size;
        memcpy(dst_ptr, &s.num_rows, kRowCountPrefixBytes);
        dst_ptr += kRowCountPrefixBytes;
        for (const auto* node = s.head; node != nullptr; node = node->next) {
            memcpy(dst_ptr, node->data(), node->size);
            dst_ptr += node->size;
        }
        // Catches total_bytes drifting from the node sizes, which would short-fill the blob and
        // leave an uninitialized tail rather than overflow anything.
        DCHECK_EQ(dst_ptr, bytes.data() + old_size + kRowCountPrefixBytes + total);

        bin->get_offset().push_back(bytes.size());
        _push_not_null(to);
        if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
            const size_t num_fields = ctx->get_arg_types().size();
            _verify_slice_roundtrip(ctx, bin, bin->get_offset().size() - 2, num_fields, _null_bitmap_bytes(num_fields),
                                    "serialize_to_column");
        }
        _clear_list(s);
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& s = this->data(const_cast<AggDataPtr>(state));
        _maybe_warn(ctx, s.num_rows);
        size_t num_fields = ctx->get_arg_types().size();
        size_t nbm = _null_bitmap_bytes(num_fields);
        size_t num_order_by = ctx->get_is_asc_order().size();
        size_t num_agg_cols = num_fields - num_order_by;

        // num_rows can come from a serialized prefix, so it is only a hint here. A row is at least
        // its null bitmap, so the payload in hand caps how many rows we reserve for.
        const size_t max_possible_rows = nbm > 0 ? s.total_bytes / nbm : 0;
        MutableColumns tmp =
                _deserialize_list(ctx, s.head, num_fields, nbm, std::min<uint64_t>(s.num_rows, max_possible_rows));
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
        _push_not_null(to);

        for (size_t i = 0; i < num_agg_cols; ++i) {
            auto* field_column = struct_column->fields_column()[i].get();
            auto* array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(field_column));
            _push_not_null(field_column);
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
        _clear_list(s);
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        auto* bin = ColumnHelper::get_binary_column(dst->get());
        auto& bytes = bin->get_bytes();
        auto& offsets = bin->get_offset();

        const size_t num_fields = src.size();
        const size_t nbm = _null_bitmap_bytes(num_fields);
        const auto& ser_sizes = ctx->get_multi_array_agg_column_serialization_size();

        // Size the whole chunk up front so `bytes` grows once instead of once per row. Each output
        // row is a standalone blob of one row, hence the prefix per row.
        size_t total = 0;
        for (size_t row = 0; row < chunk_size; ++row) {
            size_t row_size = nbm;
            for (size_t i = 0; i < num_fields; ++i) {
                row_size += _field_size(src[i].get(), row, ser_sizes[i]);
            }
            total += kRowCountPrefixBytes + row_size;
        }

        if (UNLIKELY(_would_overflow_intermediate(ctx, bin, total, "convert_to_serialize_format"))) {
            return;
        }

        const size_t old_size = bytes.size();
        bytes.resize(old_size + total);
        offsets.reserve(offsets.size() + chunk_size);
        // Safe to cache: `bytes` is not resized again below.
        uint8_t* const buf_start = bytes.data();
        uint8_t* dst_ptr = buf_start + old_size;

        const uint64_t one = 1;
        NullableColumn* null_out = dst->get()->is_nullable() ? down_cast<NullableColumn*>(dst->get()) : nullptr;
        for (size_t row = 0; row < chunk_size; ++row) {
            memcpy(dst_ptr, &one, kRowCountPrefixBytes);
            dst_ptr += kRowCountPrefixBytes;

            uint8_t* bitmap = dst_ptr;
            memset(bitmap, 0, nbm);
            dst_ptr += nbm;
            for (size_t i = 0; i < num_fields; ++i) {
                _write_field(bitmap, dst_ptr, i, src[i].get(), row, ser_sizes[i]);
            }

            offsets.push_back(static_cast<uint32_t>(dst_ptr - buf_start));
            if (null_out != nullptr) {
                null_out->null_column_data().emplace_back(0);
            }
            if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
                if (_verify_slice_roundtrip(ctx, bin, offsets.size() - 2, num_fields, nbm,
                                            "convert_to_serialize_format"))
                    return;
            }
        }
    }

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        _clear_list(this->data(state));
    }

    std::string get_name() const override { return "multi_array_agg_v3"; }

private:
    static constexpr size_t kRowCountPrefixBytes = sizeof(uint64_t);
    static constexpr size_t kMaxCorruptionDumpBytes = 500;

    // V3 reuses V2's debug-level session variable instead of adding one of its own, hence the
    // get_multi_array_agg_v2_debug_level() calls below.

    static constexpr size_t _null_bitmap_bytes(size_t num_fields) { return (num_fields + 7) / 8; }

    // Single teardown for every path that drops the list, so the fields cannot drift apart.
    static void _clear_list(MultiArrayAggV3AggregateState& s) {
        s.head = nullptr;
        s.num_rows = 0;
        s.total_bytes = 0;
    }

    // Allocates [node header][size bytes] as one block, front-inserts it, and returns the data
    // region. Requested at the node's own alignment rather than MemPool's 16-byte default: only
    // the header needs alignment, the row bytes are only ever memcpy'd.
    static uint8_t* _alloc_row_node(FunctionContext* ctx, MultiArrayAggV3AggregateState& s, size_t size) {
        constexpr size_t node_sz = sizeof(MultiArrayAggV3ListNode);
        // Would silently truncate into node->size otherwise.
        if (UNLIKELY(size > std::numeric_limits<uint32_t>::max())) {
            ctx->set_error(fmt::format("multi_array_agg_v3: single node of {} bytes exceeds the 4 GiB node limit", size)
                                   .c_str(),
                           false);
            return nullptr;
        }
        uint8_t* block = ctx->mem_pool()->allocate_aligned(static_cast<int64_t>(node_sz + size),
                                                           alignof(MultiArrayAggV3ListNode));
        if (UNLIKELY(block == nullptr)) {
            ctx->set_error("multi_array_agg_v3: failed to allocate row node from mem_pool", false);
            return nullptr;
        }
        auto* node = new (block) MultiArrayAggV3ListNode;
        node->size = static_cast<uint32_t>(size);
        node->next = s.head;
        s.head = node;
        return block + node_sz;
    }

    static size_t _field_size(const Column* col, size_t row, int ser_size) {
        if (_is_null(col, row)) {
            return 0;
        }
        if (ser_size != 0) {
            return static_cast<size_t>(ser_size);
        }
        auto [dc, ar] = _unwrap(col, row);
        return dc->serialize_size(ar);
    }

    static void _write_field(uint8_t* bitmap, uint8_t*& pos, size_t i, const Column* col, size_t row, int ser_size) {
        if (_is_null(col, row)) {
            bitmap[i / 8] |= (1 << (i % 8));
            return;
        }
        auto [dc, ar] = _unwrap(col, row);
        if (ser_size == 0) {
            pos += dc->serialize(ar, pos);
        } else {
            // Only dict-encoded columns take this path, and those are always INT32.
            _serialize_fixed_length(static_cast<const Int32Column*>(dc)->get_data()[ar], pos, ser_size);
            pos += ser_size;
        }
    }

    // Inverse of _write_field. Returns false, without advancing pos, if the field would run past end.
    static bool _read_field(FunctionContext* ctx, size_t i, const uint8_t* bitmap, NullableColumn* nullable,
                            const uint8_t*& pos, const uint8_t* end, int ser_size) {
        const bool is_null = (bitmap[i / 8] >> (i % 8)) & 1;
        if (is_null) {
            nullable->append_nulls(1);
            return true;
        }
        if (ser_size != 0) {
            if (UNLIKELY(static_cast<size_t>(end - pos) < static_cast<size_t>(ser_size))) {
                return false;
            }
            nullable->null_column_data().emplace_back(0);
            static_cast<Int32Column*>(nullable->data_column().get())->append(_deserialize_fixed_length(pos, ser_size));
            pos += ser_size;
            return true;
        }
        const size_t field_size = _serialized_field_size(pos, ctx->get_arg_type(i)->type, end);
        if (UNLIKELY(static_cast<size_t>(end - pos) < field_size)) {
            return false;
        }
        nullable->null_column_data().emplace_back(0);
        pos = nullable->data_column()->deserialize_and_append(pos);
        return true;
    }

    static void _push_not_null(Column* col) {
        if (col->is_nullable()) {
            down_cast<NullableColumn*>(col)->null_column_data().emplace_back(0);
        }
    }

    // Both callers pass what they are about to add, so a group may hold exactly `limit` rows either
    // way. `added` is tested first: it can come from an untrusted prefix and would wrap `limit - added`.
    static bool _check_size_limit(FunctionContext* ctx, uint64_t current, uint64_t added) {
        const uint64_t limit = static_cast<uint64_t>(ctx->get_multi_array_agg_max_array_length());
        if (UNLIKELY(added > limit || current > limit - added)) {
            ctx->set_error(("size limit (" + std::to_string(ctx->get_multi_array_agg_max_array_length()) +
                            ") of multi_array_agg_v3 is reached")
                                   .c_str());
            return true;
        }
        return false;
    }

    static void _maybe_warn(FunctionContext* ctx, uint64_t num_rows) {
        int64_t warn_limit = ctx->get_multi_array_agg_warn_array_length();
        if (UNLIKELY(warn_limit > 0 && num_rows >= static_cast<uint64_t>(warn_limit))) {
            LOG(WARNING) << "MULTI_ARRAY_AGG_V3 (" << print_id(ctx->state()->query_id()) << "): warn limit ("
                         << warn_limit << ") is reached, total " << num_rows << " rows";
        }
    }

    // The intermediate BinaryColumn has uint32_t offsets, so check before appending, not after.
    static bool _would_overflow_intermediate(FunctionContext* ctx, const BinaryColumn* bin, size_t add,
                                             const char* origin) {
        size_t cur = bin->get_bytes().size();
        size_t limit = static_cast<size_t>(Column::MAX_CAPACITY_LIMIT);
        if (add >= limit || cur + add >= limit) {
            ctx->set_error(
                    fmt::format(
                            "multi_array_agg_v3: intermediate VARBINARY exceeds 4 GiB in {} (current={}, adding={})",
                            origin, cur, add)
                            .c_str(),
                    false);
            return true;
        }
        return false;
    }

    static bool _read_row_count_prefix(FunctionContext* ctx, const Slice& slice, const char* origin, uint64_t* out) {
        if (UNLIKELY(slice.size < kRowCountPrefixBytes)) {
            ctx->set_error(fmt::format("multi_array_agg_v3: corrupted serialized data in {} "
                                       "(slice.size={} smaller than row-count prefix)",
                                       origin, slice.size)
                                   .c_str(),
                           false);
            return false;
        }
        memcpy(out, slice.data, kRowCountPrefixBytes);
        return true;
    }

    // Debug: slice bytes as comma-separated decimal codes.
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

    // Verbose when debug_level > 0, one line otherwise.
    static void _emit_corruption_error(FunctionContext* ctx, const char* origin, const Slice& slice, const uint8_t* pos,
                                       size_t row_size, size_t num_fields, size_t nbm) {
        if (UNLIKELY(ctx->get_multi_array_agg_v2_debug_level() > 0)) {
            size_t off = static_cast<size_t>(pos - reinterpret_cast<const uint8_t*>(slice.data));
            ctx->set_error(fmt::format("multi_array_agg_v3: corrupted serialized data in {} (num_fields={}, nbm={}, "
                                       "slice.size={}, pos_offset={}, row_size={}, pos+row_size={}, bytes=[{}])",
                                       origin, num_fields, nbm, slice.size, off, row_size, off + row_size,
                                       _slice_bytes_as_codes(slice, kMaxCorruptionDumpBytes))
                                   .c_str(),
                           false);
        } else {
            ctx->set_error(fmt::format("multi_array_agg_v3: corrupted serialized data in {}", origin).c_str(), false);
        }
    }

    // Checks that a blob parses into exactly its prefixed row count. Debug only: it walks with
    // _compute_row_size, which does not bounds-check.
    static bool _validate_serialized_slice(FunctionContext* ctx, const Slice& slice, size_t num_fields, size_t nbm,
                                           const char* origin) {
        uint64_t expected_rows;
        if (UNLIKELY(!_read_row_count_prefix(ctx, slice, origin, &expected_rows))) {
            return true;
        }
        const uint8_t* pos = reinterpret_cast<const uint8_t*>(slice.data) + kRowCountPrefixBytes;
        const uint8_t* end = reinterpret_cast<const uint8_t*>(slice.data) + slice.size;
        uint64_t walked = 0;
        while (pos < end) {
            size_t row_size = _compute_row_size(ctx, pos, num_fields, nbm, end);
            if (static_cast<size_t>(end - pos) < row_size) {
                _emit_corruption_error(ctx, origin, slice, pos, row_size, num_fields, nbm);
                return true;
            }
            pos += row_size;
            ++walked;
        }
        if (UNLIKELY(walked != expected_rows)) {
            ctx->set_error(fmt::format("multi_array_agg_v3: corrupted serialized data in {} "
                                       "(prefix={} but parsed {} rows)",
                                       origin, expected_rows, walked)
                                   .c_str(),
                           false);
            return true;
        }
        return false;
    }

    // Debug: re-read what was just appended, through the same get_slice() path merge() uses.
    static bool _verify_slice_roundtrip(FunctionContext* ctx, const BinaryColumn* bin, size_t row_idx,
                                        size_t num_fields, size_t nbm, const char* origin) {
        return _validate_serialized_slice(ctx, bin->get_slice(row_idx), num_fields, nbm, origin);
    }

    // Byte size of one serialized row, by skipping over its fields rather than decoding them.
    // Debug only: nothing past the null bitmap is bounds-checked.
    static size_t _compute_row_size(FunctionContext* ctx, const uint8_t* data, size_t num_fields, size_t nbm,
                                    const uint8_t* end) {
        if (static_cast<size_t>(end - data) < nbm) {
            return static_cast<size_t>(end - data) + 1;
        }
        const uint8_t* bitmap = data;
        const uint8_t* pos = data + nbm;

        for (size_t i = 0; i < num_fields; ++i) {
            bool is_null = (bitmap[i / 8] >> (i % 8)) & 1;
            if (is_null) {
                continue;
            }
            const size_t remaining = static_cast<size_t>(end - pos);
            int serialization_size = ctx->get_multi_array_agg_column_serialization_size()[i];
            const size_t field_size = serialization_size > 0
                                              ? static_cast<size_t>(serialization_size)
                                              : _serialized_field_size(pos, ctx->get_arg_type(i)->type, end);
            // A corrupt length prefix can be arbitrarily large. Stop here rather than walking pos
            // past end, which would make the next _serialized_field_size see a negative end - pos,
            // read it as a huge size_t, and memcpy the length prefix from out of bounds.
            if (UNLIKELY(field_size > remaining)) {
                return static_cast<size_t>(end - data) + 1;
            }
            pos += field_size;
        }
        return static_cast<size_t>(pos - data);
    }

    // Byte size of a non-dict field as serialized. Anything not fixed-width is [uint32_t len][data].
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
            if (UNLIKELY(static_cast<size_t>(end - pos) < sizeof(uint32_t))) {
                return static_cast<size_t>(end - pos) + 1;
            }
            uint32_t len;
            memcpy(&len, pos, sizeof(uint32_t));
            return sizeof(uint32_t) + len;
        }
        }
    }

    // Walks the list into per-field columns. Unlike the debug walkers, every field is bounds-checked.
    static MutableColumns _deserialize_list(FunctionContext* ctx, const MultiArrayAggV3ListNode* head,
                                            size_t num_fields, size_t nbm, size_t reserve_hint) {
        auto make_cols = [&]() {
            MutableColumns cols;
            cols.reserve(num_fields);
            for (size_t i = 0; i < num_fields; ++i) {
                auto col = ColumnHelper::create_column(*ctx->get_arg_type(i), true);
                if (reserve_hint > 0) {
                    col->reserve(reserve_hint);
                }
                cols.emplace_back(std::move(col));
            }
            return cols;
        };
        MutableColumns cols = make_cols();

        const auto& ser_sizes = ctx->get_multi_array_agg_column_serialization_size();

        for (const auto* node = head; node != nullptr; node = node->next) {
            const Slice node_slice{node->data(), node->size};
            auto fail = [&](const uint8_t* p) {
                _emit_corruption_error(ctx, "finalize", node_slice, p, 0, num_fields, nbm);
            };
            const uint8_t* pos = node->data();
            const uint8_t* end = pos + node->size;
            while (pos < end) {
                if (UNLIKELY(static_cast<size_t>(end - pos) < nbm)) {
                    fail(pos);
                    return make_cols();
                }
                const uint8_t* bitmap = pos;
                pos += nbm;

                for (size_t i = 0; i < num_fields; ++i) {
                    auto* nullable = down_cast<NullableColumn*>(cols[i].get());
                    if (UNLIKELY(!_read_field(ctx, i, bitmap, nullable, pos, end, ser_sizes[i]))) {
                        fail(pos);
                        return make_cols();
                    }
                }
            }
            if (UNLIKELY(pos != end)) {
                fail(pos);
                return make_cols();
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
