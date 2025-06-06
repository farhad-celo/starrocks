#pragma once

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exec/sorting/sort_helper.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "types/logical_type_infra.h"

namespace starrocks {

namespace celonis {
class RowAccessor {
public:
    using SliceSizeType = uint32_t;

    RowAccessor(FunctionContext* ctx) : ctx_(ctx) {};
    virtual ~RowAccessor() = default;

    // Seeks to the next column and returns true if it is NULL.
    virtual bool seek_and_is_null() = 0;

    // This must be called once after seek_and_is_null() returns false;
    virtual Datum get() const = 0;

    // Rewinds the current pointer to the beginning.
    virtual void rewind() = 0;

    // Seeks to the next column.
    void seek() {
        if (!seek_and_is_null()) {
            get();
        }
    }

    // Returns size when the row is serialized.
    // As a side effect, it changes the current pointer.
    virtual size_t serialized_size() {
        rewind();
        size_t result = 0;
        for (int i = 0; i < ctx_->get_num_args(); ++i) {
            result += sizeof(uint8_t); // Is NULL
            if (seek_and_is_null()) {
                continue;
            }
            Datum datum = get(); // This must be called once if not null.
            auto logical_type = ctx_->get_arg_type(i)->type;
            switch (logical_type) {
                case TYPE_VARCHAR:
                    result += sizeof(SliceSizeType) + datum.get_slice().size;
                    break;
#define M(type) \
                case type: \
                    result += sizeof(RunTimeCppType<type>); \
                    break;

                APPLY_FOR_ALL_NUMBER_TYPE(M)
                M(TYPE_DATETIME)
#undef M
                default:
                    break;
            }
        }
        return result;
    }

    // Serializes the row.
    // As a side effect, it changes the current pointer.
    virtual void serialize(uint8_t* dst) {
        rewind();
        for (int i = 0; i < ctx_->get_num_args(); ++i) {
            uint8_t is_null = seek_and_is_null();
            memcpy(dst, &is_null, sizeof(uint8_t));
            dst += sizeof(uint8_t);
            if (is_null) {
                continue;
            }
            Datum datum = get(); // This must be called once if not null.
            auto logical_type = ctx_->get_arg_type(i)->type;
            switch (logical_type) {
                case TYPE_VARCHAR: {
                    auto slice = datum.get_slice();
                    SliceSizeType size = slice.size;
                    memcpy(dst, &size, sizeof(SliceSizeType));
                    dst += sizeof(SliceSizeType);
                    memcpy(dst, slice.data, slice.size);
                    dst += slice.size;
                    break;
                }
#define M(type) \
                case type: {\
                    RunTimeCppType<type> value = datum.get<RunTimeCppType<type>>(); \
                    memcpy(dst, &value, sizeof(RunTimeCppType<type>)); \
                    dst += sizeof(RunTimeCppType<type>); \
                    break; \
                }

                APPLY_FOR_ALL_NUMBER_TYPE(M)
                M(TYPE_DATETIME)
#undef M
                default:
                    break;
            }
        }
    }

protected:
    FunctionContext* ctx_;
};

class ColumnPointersRowAccessor : public RowAccessor {
public:
    ColumnPointersRowAccessor(FunctionContext* ctx, const Column** columns, size_t row_num)
            : RowAccessor(ctx), columns_(columns), row_num_(row_num), index_(-1) {}

    bool seek_and_is_null() override {
        index_++;
        return columns_[index_]->is_null(row_num_);
    }

    Datum get() const override { return columns_[index_]->get(row_num_); }

    void rewind() override { index_ = -1; }

private:
    const Column **columns_;
    size_t row_num_;
    int index_;
};

class ColumnsRowAccessor : public RowAccessor {
public:
    ColumnsRowAccessor(FunctionContext* ctx, const Columns& columns, size_t row_num)
            : RowAccessor(ctx), columns_(columns), row_num_(row_num), index_(-1) {}

    bool seek_and_is_null() override {
        index_++;
        return columns_[index_]->is_null(row_num_);
    }

    Datum get() const override { return columns_[index_]->get(row_num_); }

    void rewind() override { index_ = -1; }

private:
    const Columns& columns_;
    size_t row_num_;
    int index_;
};

class SerializedRowAccessor : public RowAccessor {
public:
    explicit SerializedRowAccessor(FunctionContext* ctx, const uint8_t* row, size_t length)
            : RowAccessor(ctx), row_(row), size_(length), index_(-1), current_(row) {}

    bool seek_and_is_null() override {
        index_++;
        uint8_t is_null;
        memcpy(&is_null, current_, sizeof(uint8_t));
        current_ += sizeof(uint8_t);
        return is_null;
    }

    Datum get() const override {
        auto logical_type = ctx_->get_arg_type(index_)->type;
        switch (logical_type) {
            case TYPE_VARCHAR: {
                SliceSizeType size;
                memcpy(&size, current_, sizeof(SliceSizeType));
                current_ += sizeof(SliceSizeType);
                auto value = Slice(current_, size);
                current_ += size;
                return value;
            }
#define M(type) \
            case type: { \
                RunTimeCppType<type> value; \
                memcpy(&value, current_, sizeof(RunTimeCppType<type>)); \
                current_ += sizeof(RunTimeCppType<type>); \
                return value; \
            }

            APPLY_FOR_ALL_NUMBER_TYPE(M)
            M(TYPE_DATETIME)
#undef M
            default:
                throw std::runtime_error(fmt::format("Unsupported column type {}", logical_type));
        }
    }

    void rewind() override {
        index_ = -1;
        current_ = row_;
    }

    size_t serialized_size() override { return size_; }

    virtual void serialize(uint8_t* dst) override {
        memcpy(dst, row_, size_);
    }

private:
    const uint8_t* row_;
    size_t size_;
    int index_;
    mutable const uint8_t* current_;
};
} // namespace celonis

template <bool is_first>
struct CelonisSortedFirstLastAggregateState {
    void set_new_row(celonis::RowAccessor& row_accessor) {
        buffer.resize(row_accessor.serialized_size());
        row_accessor.serialize(buffer.data());
    }

    void update(FunctionContext* ctx, celonis::RowAccessor& new_row_accessor) {
        if (buffer.empty()) {
            set_new_row(new_row_accessor);
            return;
        }

        auto row_accessor = celonis::SerializedRowAccessor{ctx, buffer.data(), buffer.size()};
        // Skip the first column
        row_accessor.seek();
        new_row_accessor.rewind();
        new_row_accessor.seek();

        const auto& is_asc_order = ctx->get_is_asc_order();
        int num_args = ctx->get_num_args();
        for (int i = 1; i < num_args; ++i) {
            auto order_index = i - 1;
            if (row_accessor.seek_and_is_null()) {
                if (new_row_accessor.seek_and_is_null()) {
                    continue;
                }
                return;
            }
            Datum datum = row_accessor.get(); // This must be called once if not null.
            if (new_row_accessor.seek_and_is_null()) {
                return;
            }
            Datum new_datum = new_row_accessor.get(); // This must be called once if not null.
            int cmp = 0;
            auto logical_type = ctx->get_arg_type(i)->type;
            switch (logical_type) {
#define M(type) \
                case type: {\
                    cmp = SorterComparator<RunTimeCppType<type>>::compare( \
                            datum.get<RunTimeCppType<type>>(), new_datum.get<RunTimeCppType<type>>()); \
                    break; \
                }

                APPLY_FOR_ALL_NUMBER_TYPE(M)
                M(TYPE_DATETIME)
                M(TYPE_VARCHAR)
#undef M
                default:
                    throw std::runtime_error(fmt::format("Unsupported column type {}", logical_type));
            }
            if (cmp == 0) {
                continue;
            } else if ((cmp < 0) == is_asc_order[order_index]) {
                if constexpr (!is_first) {
                    set_new_row(new_row_accessor);
                }
                return;
            } else {
                if constexpr (is_first) {
                    set_new_row(new_row_accessor);
                }
                return;
            }
        }
    }

    void deserialize_and_merge(FunctionContext* ctx, const uint8_t* src, size_t len) {
        if (len == 0) {
            return;
        }
        auto row_accessor = celonis::SerializedRowAccessor{ctx, src, len};
        update(ctx, row_accessor);
    }

    ~CelonisSortedFirstLastAggregateState() {}

    raw::RawVector<uint8_t> buffer;
};

/**
 * @param: col [ORDER BY col0 [DESC | ASC] [NULLS FIRST | NULLS LAST] ...]
 * @paramType: Any type
 * @return: col type
 * col: the column whose values to choose as first or last.
        Rows with NULL values are ignored, so they do not influence the result.
        If all the values are NULL, the result is NULL.
 * col0: the column which decides the order of col. There may be more than one ORDER BY column.
         [DESC | ASC]: specifies whether to sort the elements in ascending order (default) or descending order of col0.
         [NULLS FIRST | NULLS LAST]: this option is currently ignored; see also the Jira ticket CPL-14271. Thus,
                                     for CELONIS_SORTED_FIRST (CELONIS_SORTED_LAST), NULL values are
                                     considered to be greater than (less than) any other value.
 */
template <bool is_first>
class CelonisSortedFirstLastAggregateFunction
        : public AggregateFunctionBatchHelper<CelonisSortedFirstLastAggregateState<is_first>,
                                              CelonisSortedFirstLastAggregateFunction<is_first>> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override {
        auto& state_impl = this->data(state);
        state_impl.buffer.clear();
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override {
        if (columns[0]->is_null(row_num)) return;
        auto row_accessor = celonis::ColumnPointersRowAccessor(ctx, columns, row_num);
        this->data(state).update(ctx, row_accessor);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        if (column->is_null(row_num)) {
            return;
        }
        const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
        auto slice = input_column->get_slice(row_num);
        this->data(state).deserialize_and_merge(ctx, (const uint8_t*)slice.data, slice.size);
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& state_impl = this->data(state);
        BinaryColumn* binary_column = nullptr;
        if (to->is_nullable()) {
            auto* nullable_column = down_cast<NullableColumn*>(to);
            if (state_impl.buffer.empty()) {
                nullable_column->append_default();
                return;
            }
            nullable_column->null_column_data().push_back(0);
            binary_column = down_cast<BinaryColumn*>(nullable_column->data_column().get());
        } else {
            binary_column = down_cast<BinaryColumn*>(to);
        }
        binary_column->append(Slice(state_impl.buffer.data(), state_impl.buffer.size()));
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const final {
        BinaryColumn* binary_column = nullptr;
        if ((*dst)->is_nullable()) {
            auto* dst_nullable_column = down_cast<NullableColumn*>((*dst).get());
            binary_column = down_cast<BinaryColumn*>(dst_nullable_column->data_column().get());

            if (src[0]->is_nullable()) {
                dst_nullable_column->null_column_data() =
                        down_cast<const NullableColumn*>(src[0].get())->immutable_null_column_data();
            } else {
                dst_nullable_column->null_column_data().resize(chunk_size, 0);
            }
        } else {
            binary_column = down_cast<BinaryColumn*>((*dst).get());
        }

        Bytes& bytes = binary_column->get_bytes();
        binary_column->get_offset().resize(chunk_size + 1);
        size_t bytes_size = bytes.size();
        bool has_null = false;
        for (size_t i = 0; i < chunk_size; ++i) {
            if (src[0]->is_null(i)) {
                has_null = true;
            } else {
                auto row_accessor = celonis::ColumnsRowAccessor(ctx, src, i);
                size_t new_bytes_size = bytes_size + row_accessor.serialized_size();
                bytes.resize(new_bytes_size);
                row_accessor.serialize(bytes.data() + bytes_size);
                bytes_size = new_bytes_size;
            }
            binary_column->get_offset()[i + 1] = bytes_size;
        }
        if (has_null && (*dst)->is_nullable()) {
            auto* dst_nullable_column = down_cast<NullableColumn*>((*dst).get());
            dst_nullable_column->set_has_null(true);
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto& state_impl = this->data(state);
        if (state_impl.buffer.empty()) {
            to->append_default();
            return;
        }
        auto row_accessor = celonis::SerializedRowAccessor{ctx, state_impl.buffer.data(), state_impl.buffer.size()};
        // If the value is NULL or an order by column is NULL, we return NULL.
        for (int i = 0; i < ctx->get_num_args(); ++i) {
            if (row_accessor.seek_and_is_null()) {
                to->append_default();
                return;
            }
            row_accessor.get();  // This must be called once if not null to move the pointer.
        }
        row_accessor.rewind();
        row_accessor.seek_and_is_null();
        to->append_datum(row_accessor.get());
    }

    std::string get_name() const override {
        if constexpr (is_first) {
            return "celonis_sorted_first";
        } else {
            return "celonis_sorted_last";
        }
     }
};

} // namespace starrocks
