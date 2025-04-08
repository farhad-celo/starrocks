#pragma once

#include "column/datum.h"
#include "exprs/celonis/serialization_utils.h"

namespace starrocks {
namespace celonis {

using RowIdxType = std::size_t;
using ColumnIdxType = std::size_t;

template <typename T>
concept RowBufferSupportedType =
        SerializableType<T> && (std::is_arithmetic_v<T> || std::is_same_v<T, Slice> || std::is_same_v<T, DateValue> ||
                                std::is_same_v<T, TimestampValue> || std::is_same_v<T, DecimalV2Value>);

class RowBufferRowAccessor {
public:
    RowBufferRowAccessor(ColumnIdxType num_columns) : num_columns_{num_columns} {}

    virtual ~RowBufferRowAccessor() = default;

    virtual Datum get(ColumnIdxType idx) const = 0;

    ColumnIdxType num_columns() const { return num_columns_; };

private:
    ColumnIdxType num_columns_;
};

/*
* A buffer for storing k rows of the given type signature. The buffer has k slots, slot ids must be used to set or
* access rows. Note that this implementation assumes a small k and is not optimized for handling large k.
*/
class RowBuffer {
public:
    /**
     * Constructor.
     * @param size Number of slots in the buffer.
     * @param types Type signature of the stored rows.
     */
    RowBuffer(RowIdxType size, std::vector<LogicalType> types);

    /**
     * Get a specific column value for the given row slot.
     * @param slot_id Slot id referencing a stored row.
     * @param column_idx Column idx referencing a column of a stored row.
     * @return Value wrapped in a datum typed based on the stored type signature.
     */
    Datum get(RowIdxType slot_id, ColumnIdxType column_idx) const;

    /**
     * Set the slot referenced by the given slot id to a new row.
     * @param slot_id Slot id referencing the slot used for storing the row.
     * @param row_accessor Accessor that abstracts the given row to be stored in the slot.
     */
    void set(RowIdxType slot_id, const RowBufferRowAccessor& row_accessor);

    /**
     * Get the number of columns per row in the row buffer.
     * @return Number of columns per row.
     */
    ColumnIdxType num_columns() const;

private:
    RowIdxType size_;
    std::vector<LogicalType> types_;
    ByteSize bytes_per_tuple_;
    std::vector<ByteSize> column_offsets_in_bytes;
    std::vector<Byte> buffer_;
    std::vector<std::string> string_slots_;
};

} // namespace celonis
} // namespace starrocks