#include "exprs/celonis/peek_merged_sorted_arrays.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "exprs/celonis/util.h"

namespace starrocks {

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisPeekMergedSortedArrays<LT>::peek_merged_sorted_arrays([[maybe_unused]]starrocks::FunctionContext* context,
                                                             const starrocks::Columns& columns) {
    if (columns[0]->only_null()) {
        return Status::InvalidArgument("input_array column should not be NULL literal.");
    }
    if (columns[1]->only_null()) {
        return Status::InvalidArgument("timestamp_array column should not be NULL literal.");
    }
    if (columns[2]->only_null()) {
        return Status::InvalidArgument("size_array column should not be NULL literal.");
    }
    DCHECK(columns.size() == 4 || columns.size() == 5);
    size_t chunk_size = columns[0]->size();
    ColumnPtr timestamp_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[1]);
    if (timestamp_column->has_null()) {
        return Status::InvalidArgument("timestamp_array should not be NULL.");
    }
    UnnestedArrayData timestamp_array_data = prepare_array_input(timestamp_column.get());
    const auto& timestamp_offsets = timestamp_array_data.offsets->get_data().data();

    std::vector<DatumKey> secondary_orders;
    const bool has_secondary_order = (columns.size() == 5) && (!columns[4]->has_null());
    if (has_secondary_order) {
        secondary_orders.reserve(timestamp_offsets[chunk_size]);
        ColumnPtr secondary_order_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[4]);
        UnnestedArrayData secondary_order_array_data = prepare_array_input(secondary_order_column.get());
        const auto& secondary_order_offsets = secondary_order_array_data.offsets->get_data().data();
        for (auto row = 0; row < chunk_size; ++row) {
            const auto start = timestamp_offsets[row];
            const auto end = timestamp_offsets[row + 1];
            if (secondary_order_offsets[row] != start || secondary_order_offsets[row + 1] != end) {
                return Status::InvalidArgument(
                        "If provided, the size of secondary_order_array and timestamp_array should not be different.");
            }
        }
        for (auto row = 0; row < chunk_size; ++row) {
            auto array = secondary_order_column->get(row).get_array();
            for (const auto& item: array) {
                secondary_orders.push_back(item.convert2DatumKey());
            }
        }
    }

    ColumnPtr size_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[2]);
    if (size_column->has_null()) {
        return Status::InvalidArgument("size_array should not be NULL.");
    }
    UnnestedArrayData size_array_data = prepare_array_input(size_column.get());
    if (size_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("size_array should not have NULL elements.");
    }
    const auto& sizes = down_cast<const RunTimeColumnType<TYPE_INT>&>(*size_array_data.elements).get_data().data();
    const auto& size_offsets = size_array_data.offsets->get_data().data();

    // TODO(y.zhang): Support NULL priority column.
    ColumnPtr priority_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[3]);
    if (priority_column->has_null()) {
        return Status::InvalidArgument("priority_array should not be NULL.");
    }
    UnnestedArrayData priority_array_data = prepare_array_input(priority_column.get());
    if (priority_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("priority_array should not have NULL elements.");
    }
    const auto& priorities =
            down_cast<const RunTimeColumnType<TYPE_INT>&>(*priority_array_data.elements).get_data().data();
    const auto& priority_offsets = priority_array_data.offsets->get_data().data();

    if (columns[0]->is_nullable()) {
        if (columns[0]->has_null()) {
            return Status::InvalidArgument("input_array should not be null.");
        }
    }
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& src_elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& src_offsets = array_data.offsets->get_data().data();
    ColumnBuilder<LT> result(chunk_size);

    for (size_t row = 0; row < chunk_size; row++) {
        size_t src_timestamp_start = src_offsets[row];
        size_t src_timestamp_end = src_offsets[row + 1];
        if (timestamp_offsets[row + 1] != src_timestamp_end) {
            return Status::InvalidArgument("The size of input_array and timestamp_array should not be different.");
        }
        size_t size_priority_start = size_offsets[row];
        size_t size_priority_end = size_offsets[row + 1];
        // TODO(y.zhang): Change the implementation to support priority array has the same size as timestamp array.
        if (priority_offsets[row + 1] != size_priority_end) {
            return Status::InvalidArgument("The size of size_array and priority_array should not be different.");
        }
        std::vector<DatumKey> timestamp_keys;
        timestamp_keys.reserve(timestamp_offsets[row + 1] - timestamp_offsets[row]);
        auto array = timestamp_column->get(row).get_array();
        for (const auto& item: array) {
            timestamp_keys.push_back(item.convert2DatumKey());
        }
        size_t start = src_timestamp_start;
        size_t next = 0;
        size_t first_index = -1;
        size_t priority_index = -1;
        for (size_t i = size_priority_start; i < size_priority_end; i++) {
            next = start + sizes[i];
            if (next > src_timestamp_end) {
                return Status::InvalidArgument(
                        "The size of input_array and timestamp_array should not be different than the sum of "
                        "size_array.");
            }
            size_t non_null_start = start;
            // find the first non-null value
            while (non_null_start < next && array_data.null_elements != nullptr &&
                   (*array_data.null_elements)[non_null_start] == 1) {
                non_null_start++;
            }
            start = next;
            if (non_null_start == next) {
                // Skip empty or all null arrays
                continue;
            }
            if (first_index == -1) {
                first_index = non_null_start;
                priority_index = i;
            } else {
                if (timestamp_keys[non_null_start - src_timestamp_start] !=
                    timestamp_keys[first_index - src_timestamp_start]) {
                    if (timestamp_keys[non_null_start - src_timestamp_start] <
                        timestamp_keys[first_index - src_timestamp_start]) {
                        first_index = non_null_start;
                        priority_index = i;
                    }
                } else if (!has_secondary_order || secondary_orders[non_null_start] == secondary_orders[first_index]) {
                    if (priorities[i] > priorities[priority_index]) {
                        first_index = non_null_start;
                        priority_index = i;
                    }
                } else {
                    if (secondary_orders[non_null_start] < secondary_orders[first_index]) {
                        first_index = non_null_start;
                        priority_index = i;
                    }
                }
            }
        }
        if (next != src_timestamp_end) {
            return Status::InvalidArgument(
                    "The size of input_array and timestamp_array should not be different than the sum of "
                    "size_array.");
        }
        if (first_index != -1) {
            result.append(src_elements[first_index]);
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

template
class CelonisPeekMergedSortedArrays<TYPE_INT>;

template
class CelonisPeekMergedSortedArrays<TYPE_BIGINT>;

template
class CelonisPeekMergedSortedArrays<TYPE_DOUBLE>;

template
class CelonisPeekMergedSortedArrays<TYPE_DATETIME>;

template
class CelonisPeekMergedSortedArrays<TYPE_VARCHAR>;

} // namespace starrocks
