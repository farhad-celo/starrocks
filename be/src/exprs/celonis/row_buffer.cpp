#include "row_buffer.h"

#include "types/logical_type_infra.h"

namespace starrocks {
namespace celonis {

static constexpr RowIdxType MAX_ROW_BUFFER_SIZE{10'000};

namespace {

/* Dispatch functor instantiation for all relevant logical types. */
template <class Functor, class... Args>
auto dispatch(LogicalType type, Functor fun, Args&&... args) {
    if (type == TYPE_VARCHAR) {
        using ValueType = RunTimeCppType<TYPE_VARCHAR>;
        static_assert(RowBufferSupportedType<ValueType>);
        return fun.template operator()<ValueType>(std::forward<Args>(args)...);
    } else {
        return type_dispatch_aggregate(
                type,
                [&fun]<LogicalType TYPE>(Args&&... args) {
                    using ValueType = RunTimeCppType<TYPE>;
                    if (!RowBufferSupportedType<ValueType>) {
                        std::string error_string{std::string{"RowBuffer called for unsupported type "} +
                                                 typeid(ValueType).name() + "."};
                        throw std::runtime_error{error_string};
                    }
                    return fun.template operator()<ValueType>(std::forward<Args>(args)...);
                },
                std::forward<Args>(args)...);
    }
}

template <typename T>
ByteSize typed_serialization_size() {
    return static_serialization_size<T>(); // add boolean for null flag
}

template <>
ByteSize typed_serialization_size<Slice>() {
    return static_serialization_size<std::intptr_t>(); // add boolean for null flag
}

template <typename T>
ByteSize typed_serialization_size_nullable() {
    return static_serialization_size<bool>() + typed_serialization_size<T>(); // add boolean for null flag
}

template <typename T>
T typed_get(ByteBuffer& buffer) {
    return deserialize<T>(buffer);
}

template <>
Slice typed_get<Slice>(ByteBuffer& buffer) {
    static_assert(RowBufferSupportedType<Slice>);
    const std::string& string_slot{*reinterpret_cast<const std::string*>(deserialize<std::intptr_t>(buffer))};
    return Slice{string_slot.data(), string_slot.size()};
}

template <typename T>
Datum typed_get_nullable(ByteBuffer& buffer) {
    if (typed_get<bool>(buffer)) {
        return Datum{};
    }
    return typed_get<T>(buffer);
}

template <typename T>
void typed_set(MutableByteBuffer& buffer, const T& value) {
    serialize<T>(buffer, value);
}

template <>
void typed_set<Slice>(MutableByteBuffer& buffer, const Slice& slice) {
    static_assert(RowBufferSupportedType<Slice>);
    std::string& string_slot{
            *reinterpret_cast<std::string*>(deserialize<std::intptr_t>(const_cast<ByteBuffer&>(buffer)))};
    string_slot = slice.to_string();
}

template <typename T>
void typed_set_nullable(MutableByteBuffer& buffer, const Datum& value) {
    if (value.is_null()) {
        typed_set<bool>(buffer, true);
    } else {
        typed_set<bool>(buffer, false);
        typed_set<T>(buffer, value.get<T>());
    }
}

} // namespace
RowBuffer::RowBuffer(std::size_t size, std::vector<LogicalType> types) : size_{size}, types_{std::move(types)} {
    if (size < 1) {
        throw std::invalid_argument{"Row buffer cannot be constructed with size < 1."};
    } else if (size > MAX_ROW_BUFFER_SIZE) {
        throw std::invalid_argument{fmt::format("Row buffer: Given size={} is larger than maximum allowed size {}.",
                                                size, MAX_ROW_BUFFER_SIZE)};
    }
    if (types_.size() < 1) {
        throw std::invalid_argument{"Row buffer cannot be constructed for empty type signature."};
    }

    // Determine overall size and type offsets
    ByteSize written_bytes = 0;
    column_offsets_in_bytes = std::vector<ByteSize>(types_.size() + 1);
    size_t string_count{0};
    for (ColumnIdxType i{0}; i < types_.size(); i++) {
        column_offsets_in_bytes[i] = written_bytes;
        LogicalType type{types_[i]};
        if (type == TYPE_VARCHAR) {
            string_count++;
            written_bytes += typed_serialization_size_nullable<RunTimeCppType<TYPE_VARCHAR>>();
        } else {
            written_bytes += dispatch(type, [&]<typename T> { return typed_serialization_size_nullable<T>(); });
        }
    }
    column_offsets_in_bytes[types_.size()] = written_bytes;
    bytes_per_tuple_ = written_bytes;

    // Initialize the raw buffer
    ByteSize overall_size{bytes_per_tuple_ * size_};
    buffer_ = std::vector<Byte>(overall_size, 0);

    // Initialize string slots
    string_slots_ = std::vector<std::string>(string_count * size_);
    size_t current_slot_idx{0};
    for (ColumnIdxType i{0}; i < types_.size(); i++) {
        LogicalType type{types_[i]};
        if (type != TYPE_VARCHAR) {
            continue;
        }
        ByteSize column_offset{column_offsets_in_bytes[i]};
        // Assign k slots per string column
        for (RowIdxType row_idx{0}; row_idx < size_; row_idx++) {
            ByteSize row_offset{bytes_per_tuple_ * row_idx};
            MutableByteBuffer dst = buffer_.data() + column_offset + row_offset +
                                    static_serialization_size<bool>(); // Need to offset with byte for storing null flag
            const std::string* slot_ptr{&string_slots_[current_slot_idx]};
            std::memcpy(dst, &slot_ptr, sizeof(std::uintptr_t));
            current_slot_idx++;
        }
    }
}

Datum RowBuffer::get(RowIdxType slot_id, ColumnIdxType column_idx) const {
    DCHECK(slot_id < size_);
    LogicalType expected_type{types_[column_idx]};
    ByteSize row_offset{slot_id * bytes_per_tuple_};
    ByteSize column_offset{column_offsets_in_bytes[column_idx]};
    ByteBuffer buffer_loc{buffer_.data() + row_offset + column_offset};
    return dispatch(expected_type, [&]<typename T> { return typed_get_nullable<T>(buffer_loc); });
}

void RowBuffer::set(RowIdxType slot_id, const RowBufferRowAccessor& row_accessor) {
    DCHECK(types_.size() == row_accessor.num_columns());
    DCHECK(slot_id < size_);
    ByteSize row_offset{slot_id * bytes_per_tuple_};
    for (ColumnIdxType column_idx{0}; column_idx < row_accessor.num_columns(); column_idx++) {
        LogicalType expected_type{types_[column_idx]};
        ByteSize column_offset{column_offsets_in_bytes[column_idx]};
        MutableByteBuffer buffer_loc{buffer_.data() + row_offset + column_offset};
        dispatch(expected_type, [&]<typename T> { typed_set_nullable<T>(buffer_loc, row_accessor.get(column_idx)); });
    }
}

ColumnIdxType RowBuffer::num_columns() const {
    return types_.size();
}

} // namespace celonis
} // namespace starrocks