#include "exprs/celonis/array_functions.h"

#include <vector>
#include <utility>

#include "column/array_column.h"
#include "exprs/celonis/util.h"
#include "column/column_builder.h"
#include "column/column_viewer.h"

namespace starrocks {

class CelonisArrayIsSortedImpl {
public:
    static ColumnPtr evaluate(const Column& array) {
        return _array_is_sorted_generic(array);
    }

private:
    template<bool NullableElement, typename ElementColumn>
    static ColumnPtr _process(const ElementColumn& elements, const UInt32Column& offsets,
                              const NullColumn::Container* null_map_elements) {
        const size_t num_array = offsets.size() - 1;
        auto result = UInt8Column::create();
        result->resize(num_array);

        auto* result_ptr = result->get_data().data();

        auto offsets_ptr = offsets.get_data().data();
        [[maybe_unused]] auto elements_ptr = (const typename ElementColumn::ValueType*) (elements.raw_data());

        [[maybe_unused]] auto is_null = [](const NullColumn::Container* null_map, size_t idx) -> bool {
            return (*null_map)[idx] != 0;
        };

        for (size_t i = 0; i < num_array; i++) {
            const size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
            if (array_size < 2) {
                result_ptr[i] = true;
                continue;
            }

            size_t offset = offsets_ptr[i];
            const size_t offset_end = offset + array_size;

            if constexpr (NullableElement) {
                while (is_null(null_map_elements, offset) && offset < offset_end) {
                    ++offset;
                }

                if (offset == offset_end) {
                    result_ptr[i] = true;
                    continue;
                }
            }

            size_t position_prev = offset;
            for (++offset; offset < offset_end; ++offset) {
                if constexpr (NullableElement) {
                    if (is_null(null_map_elements, offset)) {
                        break;
                    }
                }

                if (elements_ptr[position_prev] > elements_ptr[offset]) {
                    break;
                }

                position_prev = offset;
            }
            result_ptr[i] = (offset == offset_end);
        }
        return result;
    }

    template<bool NullableElement>
    static ColumnPtr _array_is_sorted(const Column& array_elements, const UInt32Column& array_offsets) {
        const Column* elements_ptr = &array_elements;

        const NullColumn::Container* null_map_elements = nullptr;

        if constexpr (NullableElement) {
            const auto& nullable = down_cast<const NullableColumn&>(array_elements);
            elements_ptr = nullable.data_column().get();
            null_map_elements = &(nullable.null_column()->get_data());
        }

            // Using typeid instead of dynamic_cast, as typeid is much faster than dynamic_cast
#define HANDLE_ELEMENT_TYPE(ElementType)                                                         \
do {                                                                                             \
    if (typeid(*elements_ptr) == typeid(ElementType)) {                                          \
        return _process<NullableElement>(                                                        \
                *down_cast<const ElementType*>(elements_ptr), array_offsets, null_map_elements); \
    }                                                                                            \
} while (0)

        HANDLE_ELEMENT_TYPE(BooleanColumn);
        HANDLE_ELEMENT_TYPE(Int8Column);
        HANDLE_ELEMENT_TYPE(Int16Column);
        HANDLE_ELEMENT_TYPE(Int32Column);
        HANDLE_ELEMENT_TYPE(Int64Column);
        HANDLE_ELEMENT_TYPE(Int128Column);
        HANDLE_ELEMENT_TYPE(FloatColumn);
        HANDLE_ELEMENT_TYPE(DoubleColumn);
        HANDLE_ELEMENT_TYPE(DecimalColumn);
        HANDLE_ELEMENT_TYPE(Decimal32Column);
        HANDLE_ELEMENT_TYPE(Decimal64Column);
        HANDLE_ELEMENT_TYPE(Decimal128Column);
        HANDLE_ELEMENT_TYPE(BinaryColumn);
        HANDLE_ELEMENT_TYPE(DateColumn);
        HANDLE_ELEMENT_TYPE(TimestampColumn);
        // HANDLE_ELEMENT_TYPE(ArrayColumn);
#undef HANDLE_ELEMENT_TYPE

        // TODO(zuyu): demangle class name
        std::stringstream error;
        error << "unhandled column type in array_is_sorted" << typeid(array_elements).name() << std::endl;
        throw std::runtime_error(error.str());
    }

    // array is non-nullable.
    static ColumnPtr _array_is_sorted_non_nullable(const ArrayColumn& array) {
        bool nullable_element = false;

        const Column* elements = &array.elements();
        const UInt32Column& offsets = array.offsets();
        if (auto nullable = dynamic_cast<const NullableColumn*>(elements); nullable != nullptr) {
            // If this nullable column does NOT contain any NULL, process it as non-nullable column.
            nullable_element = nullable->has_null();
            if (!nullable_element)
                elements = nullable->data_column().get();
        }

        if (nullable_element) {
            return _array_is_sorted<true>(*elements, offsets);
        } else {
            return _array_is_sorted<false>(*elements, offsets);
        }
    }

    static ColumnPtr _array_is_sorted_generic(const Column& array) {
        // array_is_sorted(NULL) -> NULL
        if (array.only_null()) {
            auto result = NullableColumn::create(UInt8Column::create(), NullColumn::create());
            result->append_nulls(array.size());
            return result;
        }
        if (auto nullable = dynamic_cast<const NullableColumn*>(&array); nullable != nullptr) {
            auto array_col = down_cast<const ArrayColumn*>(nullable->data_column().get());
            auto result = _array_is_sorted_non_nullable(*array_col);
            DCHECK_EQ(nullable->size(), result->size());
            if (!nullable->has_null()) {
                return result;
            }
            return NullableColumn::create(std::move(result), nullable->null_column());
        }
        return _array_is_sorted_non_nullable(down_cast<const ArrayColumn&>(array));
    }
};


StatusOr<ColumnPtr>
CelonisArrayFunctions::array_is_sorted([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    const ColumnPtr& arg0 = columns[0]; // array

    return CelonisArrayIsSortedImpl::evaluate(*arg0);
}

class CelonisMergeSortedArrays {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK(columns.size() == 4 || columns.size() == 5 || columns.size() == 6);

        size_t chunk_size = columns[0]->size();

        ColumnPtr timestamp_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[1]);
        if (timestamp_column->has_null()) {
            return Status::InvalidArgument("timestamp_array should not be NULL.");
        }
        UnnestedArrayData timestamp_array_data = prepare_array_input(timestamp_column.get());
        if (timestamp_array_data.null_elements != nullptr) {
            return Status::InvalidArgument("timestamp_array should not have NULL elements.");
        }
        DCHECK(timestamp_array_data.elements->is_timestamp());
        const auto& timestamps =
                down_cast<const RunTimeColumnType<TYPE_DATETIME>&>(*timestamp_array_data.elements).get_data().data();
        const auto& timestamp_offsets = timestamp_array_data.offsets->get_data().data();

        std::vector<DatumKey> secondary_orders;
        const bool has_secondary_order = (columns.size() >= 5) && (!columns[4]->has_null());
        if (has_secondary_order) {
            secondary_orders.reserve(timestamp_offsets[chunk_size]);
            ColumnPtr secondary_order_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[4]);
            UnnestedArrayData secondary_order_array_data = prepare_array_input(secondary_order_column.get());
            if (secondary_order_array_data.null_elements != nullptr) {
                return Status::InvalidArgument("If provided, secondary_order_array should not have NULL elements.");
            }
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

        std::vector<int32_t> priorities;
        const bool has_priority = (columns.size() != 6) || (columns.size() == 6 && !columns[3]->has_null());
        if (has_priority) {
            priorities.reserve(timestamp_offsets[chunk_size]);
            const bool similar_to_size = columns.size() != 6;
            ColumnPtr priority_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[3]);
            if (priority_column->has_null()) {
                return Status::InvalidArgument("priority_array should not be NULL.");
            }
            UnnestedArrayData priority_array_data = prepare_array_input(priority_column.get());
            if (priority_array_data.null_elements != nullptr) {
                return Status::InvalidArgument("priority_array should not have NULL elements.");
            }
            const auto& priority_offsets = priority_array_data.offsets->get_data().data();
            for (auto row = 0; row < chunk_size; ++row) {
                const auto start = similar_to_size ? size_offsets[row] : timestamp_offsets[row];
                const auto end = similar_to_size ? size_offsets[row + 1] : timestamp_offsets[row + 1];
                if (priority_offsets[row] != start || priority_offsets[row + 1] != end) {
                    if (similar_to_size) {
                        return Status::InvalidArgument(
                                "If provided, the size of size_array and priority_array should not be different.");
                    } else {
                        return Status::InvalidArgument(
                                "If provided, the size of timestamp_array and priority_array should not be different.");
                    }
                }
            }
            if (similar_to_size) {
                for (auto row = 0; row < chunk_size; ++row) {
                    auto priority_array = priority_column->get(row).get_array();
                    auto size_array = size_column->get(row).get_array();
                    DCHECK_EQ(size_array.size(), priority_array.size());
                    for (auto i = 0; i < priority_array.size(); ++i) {
                        for (auto j = 0; j < size_array[i].get_int32(); ++j) {
                            priorities.push_back(priority_array[i].get_int32());
                        }
                    }
                }
            } else {
                for (auto row = 0; row < chunk_size; ++row) {
                    auto array = priority_column->get(row).get_array();
                    for (const auto& item: array) {
                        priorities.push_back(item.get_int32());
                    }
                }
            }
        } else {
            priorities.resize(timestamp_offsets[chunk_size], 0);
        }

        ColumnPtr src_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        auto* src_data_column = src_column.get();

        ColumnPtr dest_column = src_column->clone_empty();
        auto* dest_data_column = dest_column.get();

        if (src_column->is_nullable()) {
            if (src_column->has_null()) {
                return Status::InvalidArgument("input_array should not be null.");
            }
            const auto* src_nullable_column = down_cast<const NullableColumn*>(src_column.get());
            src_data_column = src_nullable_column->data_column().get();

            auto* dest_nullable_column = down_cast<NullableColumn*>(dest_column.get());
            dest_data_column = dest_nullable_column->mutable_data_column();
            auto* dest_null_column = dest_nullable_column->mutable_null_column();
            dest_null_column->get_data().resize(chunk_size, 0);
            dest_nullable_column->set_has_null(false);
        }

        const auto& src_elements = down_cast<const ArrayColumn*>(src_data_column)->elements();
        const auto& src_offsets = down_cast<const ArrayColumn*>(src_data_column)->offsets().get_data().data();
        auto* dest_elements_column = down_cast<ArrayColumn*>(dest_data_column)->elements_column().get();
        auto* dest_offsets_column = down_cast<ArrayColumn*>(dest_data_column)->offsets_column().get();

        std::vector<uint32_t> src_index;
        src_index.reserve(src_elements.size());
        int new_offset = 0;
        int64_t limit = INT64_MAX;
        for (size_t row = 0; row < chunk_size; row++) {
            if (columns.size() == 6 && !columns[5]->is_null(row)) {
                limit = columns[5]->get(row).get_int64();
                if (limit < 0) {
                    return Status::InvalidArgument("limit must not be negative.");
                }
            } else {
                limit = INT64_MAX;
            }
            size_t src_timestamp_start = src_offsets[row];
            size_t src_timestamp_end = src_offsets[row + 1];
            if (timestamp_offsets[row + 1] != src_timestamp_end) {
                return Status::InvalidArgument("The size of input_array and timestamp_array should not be different.");
            }
            size_t size_start = size_offsets[row];
            size_t size_end = size_offsets[row + 1];

            struct Array {
                Array(size_t start, size_t end, const TimestampValue* timestamp, const DatumKey* secondary_order,
                      int* priority)
                        : index(start), end(end), timestamp(timestamp), secondary_order(secondary_order),
                          priority(priority) {}

                size_t index;
                size_t end;
                const TimestampValue* timestamp;
                const DatumKey* secondary_order;
                int* priority;
            };
            struct CompareArrayElement {
                bool operator()(const Array& lhs, const Array& rhs) {
                    if (*lhs.timestamp != *rhs.timestamp) {
                        return *lhs.timestamp > *rhs.timestamp;
                    }
                    if (lhs.secondary_order == nullptr || rhs.secondary_order == nullptr) {
                        return *lhs.priority < *rhs.priority;
                    }
                    const DatumKey lhs_order = *lhs.secondary_order;
                    const DatumKey rhs_order = *rhs.secondary_order;
                    if (lhs_order == rhs_order) {
                        return *lhs.priority < *rhs.priority;
                    }
                    return lhs_order > rhs_order;
                }
            };
            std::priority_queue<Array, std::vector<Array>, CompareArrayElement> pq;
            size_t start = src_timestamp_start;
            size_t next = 0;
            for (size_t i = size_start; i < size_end; i++) {
                next = start + sizes[i];
                if (next > src_timestamp_end) {
                    return Status::InvalidArgument(
                            "The size of input_array and timestamp_array should not be different than the sum of "
                            "size_array.");
                }
                if (start == next) {
                    // Skip empty arrays.
                    continue;
                }
                if (has_secondary_order) {
                    pq.emplace(start, next, timestamps + start, secondary_orders.data() + start, priorities.data() + start);
                } else {
                    pq.emplace(start, next, timestamps + start, nullptr, priorities.data() + start);
                }
                start = next;
            }
            if (next != src_timestamp_end) {
                return Status::InvalidArgument(
                        "The size of input_array and timestamp_array should not be different than the sum of "
                        "size_array.");
            }
            int64_t cnt = 0;
            while (!pq.empty() && cnt < limit) {
                Array curr = pq.top();
                pq.pop();
                src_index.push_back(curr.index);
                new_offset++;
                cnt++;
                if (++curr.index < curr.end) {
                    curr.timestamp++;
                    curr.priority++;
                    if (curr.secondary_order != nullptr) {
                        curr.secondary_order++;
                    }
                    pq.push(curr);
                }
            }
            dest_offsets_column->get_data().push_back(new_offset);
        }
        dest_elements_column->append_selective(src_elements, src_index);
        return dest_column;
    }
};

StatusOr<ColumnPtr> CelonisArrayFunctions::merge_sorted_arrays([[maybe_unused]] FunctionContext* context,
                                                               const Columns& columns) {
    return CelonisMergeSortedArrays::process(columns);
}

class CelonisDedupSortedBy {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 2);
        if (columns[0]->only_null() || columns[1]->only_null()) {
            return Status::InvalidArgument("The arrays should not be null.");
        }

        size_t chunk_size = columns[0]->size();

        ColumnPtr src_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        ColumnPtr dest_column = src_column->clone_empty();
        ColumnPtr key_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[1]);

        if (src_column->is_nullable()) {
            const auto* src_nullable_column = down_cast<const NullableColumn*>(src_column.get());
            const auto& src_data_column = src_nullable_column->data_column_ref();
            const auto& src_null_column = src_nullable_column->null_column_ref();

            auto* dest_nullable_column = down_cast<NullableColumn*>(dest_column.get());
            auto* dest_data_column = dest_nullable_column->mutable_data_column();
            auto* dest_null_column = dest_nullable_column->mutable_null_column();
            if (src_column->has_null()) {
                dest_null_column->get_data().assign(src_null_column.get_data().begin(),
                                                    src_null_column.get_data().end());
            } else {
                dest_null_column->get_data().resize(chunk_size, 0);
            }
            dest_nullable_column->set_has_null(src_nullable_column->has_null());
            RETURN_IF_ERROR(_dedup_array_column(dest_data_column, src_data_column, key_column));
        } else {
            RETURN_IF_ERROR(_dedup_array_column(dest_column.get(), *src_column, key_column));
        }
        return dest_column;
    }

private:
    static Status _dedup_array_column(Column* dest_array_column, const Column& src_array_column,
                                      const ColumnPtr key_array_ptr) {
        ColumnPtr key_array_data = key_array_ptr;
        if (key_array_ptr->is_nullable()) { // Nullable(array(Nullable(element), offsets), null_map)
            key_array_data = down_cast<const NullableColumn*>(key_array_ptr.get())->data_column();
        }
        // key_array_data is of array(Nullable(element), offsets)

        const auto& key_element_column = down_cast<ArrayColumn*>(key_array_data.get())->elements();
        if (key_element_column.has_null()) {
            return Status::InvalidArgument("key_array should not have null elements.");
        }
        const auto& key_offsets = down_cast<ArrayColumn*>(key_array_data.get())->offsets().get_data().data();

        const auto& src_elements_column = down_cast<const ArrayColumn&>(src_array_column).elements();
        const auto& src_offsets = down_cast<const ArrayColumn&>(src_array_column).offsets().get_data().data();

        auto* dest_elements_column = down_cast<ArrayColumn*>(dest_array_column)->elements_column().get();
        auto* dest_offsets_column = down_cast<ArrayColumn*>(dest_array_column)->offsets_column().get();

        size_t chunk_size = src_array_column.size();
        std::vector<uint32_t> src_index;
        src_index.reserve(src_elements_column.size());
        int new_offset = 0;

        const auto& key_data_column = down_cast<const NullableColumn&>(key_element_column).data_column_ref();
        const auto& key_data = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(key_data_column).get_data();

        for (size_t i = 0; i < chunk_size; i++) {
            Slice prev;
            size_t start = src_offsets[i];
            size_t end = src_offsets[i + 1];
            if (end == start) {
                dest_offsets_column->get_data().push_back(new_offset);
                continue;
            }
            size_t key_start = key_offsets[i];
            size_t key_end = key_offsets[i + 1];
            // if input_array is not empty, key_array must have the same length.
            if (key_end - key_start != end - start) {
                return Status::InvalidArgument("The size of input_array and key_array should not be different.");
            }
            for (auto id = key_start; id < key_end; ++id) {
                if (id == key_start || prev != key_data[id]) {
                    src_index.push_back(id - key_start + start);
                    new_offset++;
                    prev = key_data[id];
                }
            }
            dest_offsets_column->get_data().push_back(new_offset);
        }
        dest_elements_column->append_selective(src_elements_column, src_index);
        return Status::OK();
    }
};

StatusOr<ColumnPtr> CelonisArrayFunctions::dedup_sorted_by([[maybe_unused]] FunctionContext* context,
                                                           const Columns& columns) {
    return CelonisDedupSortedBy::process(columns);
}

class CelonisArrayLag {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 2);
        if (columns[0]->only_null() || columns[1]->only_null()) {
            return Status::InvalidArgument("The input array and offset should not be null.");
        }

        size_t chunk_size = columns[0]->size();
        ColumnViewer offset_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);

        for (size_t i = 0; i < chunk_size; ++i) {
            if (offset_viewer.is_null(i)) {
                return Status::InvalidArgument("offset column must not contain null.");
            }
            if (offset_viewer.value(i) <= 0) {
                return Status::InvalidArgument("offset must be a positive integer.");
            }
        }

        ColumnPtr input_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        ColumnPtr output_column = input_column->clone_empty();

        if (input_column->is_nullable()) {
            const auto* input_nullable_column = down_cast<const NullableColumn*>(input_column.get());
            const auto& input_data_column = input_nullable_column->data_column_ref();
            const auto& input_null_column = input_nullable_column->null_column_ref();

            auto* output_nullable_column = down_cast<NullableColumn*>(output_column.get());
            auto* output_data_column = output_nullable_column->mutable_data_column();
            auto* output_null_column = output_nullable_column->mutable_null_column();
            if (input_column->has_null()) {
                output_null_column->get_data().assign(input_null_column.get_data().begin(),
                                                      input_null_column.get_data().end());
            } else {
                output_null_column->get_data().resize(chunk_size, 0);
            }
            output_nullable_column->set_has_null(input_nullable_column->has_null());
            RETURN_IF_ERROR(_array_lag(output_data_column, input_data_column, offset_viewer));
        } else {
            RETURN_IF_ERROR(_array_lag(output_column.get(), *input_column, offset_viewer));
        }
        return output_column;
    }

private:
    static Status _array_lag(Column* output_array_column, const Column& input_array_column,
                             const ColumnViewer<TYPE_BIGINT>& offset_viewer) {
        const auto& input_elements_column = down_cast<const ArrayColumn&>(input_array_column).elements();
        const auto& input_offsets = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data().data();

        auto* output_elements_column = down_cast<ArrayColumn*>(output_array_column)->elements_column().get();
        auto* output_offsets_column = down_cast<ArrayColumn*>(output_array_column)->offsets_column().get();

        for (size_t i = 0; i < input_array_column.size(); i++) {
            size_t start = input_offsets[i];
            size_t end = input_offsets[i + 1];
            int64_t lag_offset = offset_viewer.value(i);
            std::deque<size_t> window;
            for (size_t j = start; j < end; ++j) {
                if (window.size() == lag_offset) {
                    output_elements_column->append(input_elements_column, window.front(), 1);
                } else {
                    output_elements_column->append_nulls(1);
                }
                if (!input_elements_column.get(j).is_null()) {
                    window.push_back(j);
                }
                if (window.size() > lag_offset) {
                    window.pop_front();
                }
            }
        }
        output_offsets_column->get_data() = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data();
        return Status::OK();
    }
};


StatusOr<ColumnPtr> CelonisArrayFunctions::array_lag([[maybe_unused]] FunctionContext* context,
                                                     const Columns& columns) {
    return CelonisArrayLag::process(columns);
}

class CelonisArrayLead {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 2);
        if (columns[0]->only_null() || columns[1]->only_null()) {
            return Status::InvalidArgument("The input array and offset should not be null.");
        }

        size_t chunk_size = columns[0]->size();
        ColumnViewer offset_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);

        for (size_t i = 0; i < chunk_size; ++i) {
            if (offset_viewer.is_null(i)) {
                return Status::InvalidArgument("offset column must not contain null.");
            }
            if (offset_viewer.value(i) <= 0) {
                return Status::InvalidArgument("offset must be a positive integer.");
            }
        }

        ColumnPtr input_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        ColumnPtr output_column = input_column->clone_empty();

        if (input_column->is_nullable()) {
            const auto* input_nullable_column = down_cast<const NullableColumn*>(input_column.get());
            const auto& input_data_column = input_nullable_column->data_column_ref();
            const auto& input_null_column = input_nullable_column->null_column_ref();

            auto* output_nullable_column = down_cast<NullableColumn*>(output_column.get());
            auto* output_data_column = output_nullable_column->mutable_data_column();
            auto* output_null_column = output_nullable_column->mutable_null_column();
            if (input_column->has_null()) {
                output_null_column->get_data().assign(input_null_column.get_data().begin(),
                                                      input_null_column.get_data().end());
            } else {
                output_null_column->get_data().resize(chunk_size, 0);
            }
            output_nullable_column->set_has_null(input_nullable_column->has_null());
            RETURN_IF_ERROR(_array_lead(output_data_column, input_data_column, offset_viewer));
        } else {
            RETURN_IF_ERROR(_array_lead(output_column.get(), *input_column, offset_viewer));
        }
        return output_column;
    }

private:
    static Status _array_lead(Column* output_array_column, const Column& input_array_column,
                              const ColumnViewer<TYPE_BIGINT>& offset_viewer) {
        const auto& input_elements_column = down_cast<const ArrayColumn&>(input_array_column).elements();
        const auto& input_offsets = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data().data();

        auto* output_elements_column = down_cast<ArrayColumn*>(output_array_column)->elements_column().get();
        auto* output_offsets_column = down_cast<ArrayColumn*>(output_array_column)->offsets_column().get();

        for (size_t i = 0; i < input_array_column.size(); i++) {
            size_t start = input_offsets[i];
            size_t end = input_offsets[i + 1];
            int64_t lead_offset = offset_viewer.value(i);
            std::deque<size_t> window;
            std::vector<std::optional<size_t>> idxes;
            // traverse the elements reversely.
            for (size_t j = end; j-- > start;) {
                if (window.size() == lead_offset) {
                    idxes.emplace_back(window.front());
                } else {
                    idxes.emplace_back(std::nullopt);
                }
                if (!input_elements_column.get(j).is_null()) {
                    window.push_back(j);
                }
                if (window.size() > lead_offset) {
                    window.pop_front();
                }
            }
            for (auto it = idxes.rbegin(); it != idxes.rend(); ++it) {
                if (it->has_value()) {
                    output_elements_column->append(input_elements_column, it->value(), 1);
                } else {
                    output_elements_column->append_nulls(1);
                }
            }
        }
        output_offsets_column->get_data() = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data();
        return Status::OK();
    }
};


StatusOr<ColumnPtr> CelonisArrayFunctions::array_lead([[maybe_unused]] FunctionContext* context,
                                                      const Columns& columns) {
    return CelonisArrayLead::process(columns);
}

class CelonisNullToEmpty {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 1);
        if (!columns[0]->is_nullable() || !columns[0]->has_null()) {
            return columns[0]->clone();
        }
        size_t n_rows = columns[0]->size();
        ColumnPtr input_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
        ColumnPtr output_column = input_column->clone_empty();

        const auto* input_nullable_column = down_cast<const NullableColumn*>(input_column.get());
        const auto& input_data_column = input_nullable_column->data_column_ref();

        auto* output_nullable_column = down_cast<NullableColumn*>(output_column.get());
        auto* output_data_column = output_nullable_column->mutable_data_column();
        auto* output_null_column = output_nullable_column->mutable_null_column();
        output_null_column->get_data().resize(n_rows, 0);
        output_nullable_column->set_has_null(false);
        for (size_t i = 0; i < n_rows; ++i) {
            if (input_column->is_null(i)) {
                output_data_column->append_default();
            } else {
                output_data_column->append(input_data_column, i, 1);
            }
        }
        return output_column;
    }
};

StatusOr<ColumnPtr> CelonisArrayFunctions::null_to_empty([[maybe_unused]] FunctionContext* context,
                                                         const Columns& columns) {
    return CelonisNullToEmpty::process(columns);
}

Status calc_crop_impl(const Columns& columns, bool fill_one, ColumnPtr result) {
    DCHECK_EQ(columns.size(), 5);
    size_t n_rows = columns[0]->size();
    ColumnPtr activity_array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData activity_array_data = prepare_array_input(activity_array_column.get());
    DCHECK(activity_array_data.elements->is_binary());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *activity_array_data.elements).get_data().data();
    const auto& activity_offsets = activity_array_data.offsets->get_data().data();

    ColumnViewer begin_activity_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer begin_mode_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer end_activity_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    ColumnViewer end_mode_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);

    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || begin_mode_viewer.is_null(row) ||
            (begin_mode_viewer.value(row).to_string() != "ALL" && begin_activity_viewer.is_null(row)) ||
            end_mode_viewer.is_null(row) ||
            (end_mode_viewer.value(row).to_string() != "ALL" && end_activity_viewer.is_null(row))) {
            result->append_nulls(1);
            continue;
        }
        const std::string begin_mode = begin_mode_viewer.value(row).to_string();
        const std::string end_mode = end_mode_viewer.value(row).to_string();
        if (begin_mode != "FIRST" && begin_mode != "LAST" && begin_mode != "ALL") {
            return Status::InvalidArgument("begin range mode must be FIRST/LAST/ALL.");
        }
        if (end_mode != "FIRST" && end_mode != "LAST" && end_mode != "ALL") {
            return Status::InvalidArgument("end range mode must be FIRST/LAST/ALL.");
        }
        const std::string begin_activity = begin_mode == "ALL" ? "" : begin_activity_viewer.value(row).to_string();
        const std::string end_activity = end_mode == "ALL" ? "" : end_activity_viewer.value(row).to_string();

        const auto start = static_cast<int64_t>(activity_offsets[row]);
        const auto end = static_cast<int64_t>(activity_offsets[row + 1]);
        DCHECK(end >= start);
        const auto size = end - start;
        std::optional<int64_t> first_begin_index = std::nullopt;
        std::optional<int64_t> last_begin_index = std::nullopt;
        std::optional<int64_t> first_end_index = std::nullopt;
        std::optional<int64_t> last_end_index = std::nullopt;
        for (int64_t i = start; i < end; ++i) {
            if (activity_array_data.null_elements != nullptr && (*activity_array_data.null_elements)[i] != 0) {
                continue;
            }
            const std::string activity = activities[i].to_string();
            if (activity == begin_activity) {
                last_begin_index = i;
                if (!first_begin_index.has_value()) {
                    first_begin_index = i;
                }
            }
            if (activity == end_activity) {
                last_end_index = i;
                if (!first_end_index.has_value()) {
                    first_end_index = i;
                }
            }
        }
        std::optional<int64_t> begin_index = std::nullopt;
        std::optional<int64_t> end_index = std::nullopt;
        if (begin_mode == "FIRST") {
            begin_index = first_begin_index;
        } else if (begin_mode == "LAST") {
            begin_index = last_begin_index;
        } else {
            DCHECK(begin_mode == "ALL");
            begin_index = start;
        }
        if (end_mode == "FIRST") {
            end_index = first_end_index;
        } else if (end_mode == "LAST") {
            end_index = last_end_index;
        } else {
            DCHECK(end_mode == "ALL");
            end_index = end - 1;
        }
        DatumArray array;
        array.reserve(size);
        if (!begin_index.has_value() || !end_index.has_value() || begin_index.value() > end_index.value()) {
            for (int64_t j = 0; j < size; ++j) {
                array.push_back(kNullDatum);
            }
        } else {
            for (int64_t j = start; j < begin_index.value(); ++j) {
                array.push_back(kNullDatum);
            }
            for (int64_t j = begin_index.value(); j <= end_index.value(); ++j) {
                if (fill_one) {
                    array.emplace_back(1L);
                } else {
                    if (activity_array_data.null_elements != nullptr && (*activity_array_data.null_elements)[j] != 0) {
                        array.push_back(kNullDatum);
                    } else {
                        array.emplace_back(activities[j]);
                    }
                }
            }
            for (int64_t j = end_index.value() + 1; j < end; ++j) {
                array.push_back(kNullDatum);
            }
        }
        result->append_datum(array);
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisArrayFunctions::calc_crop([[maybe_unused]] FunctionContext* context,
                                                     const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    TypeDescriptor type_array_bigint;
    type_array_bigint.type = TYPE_ARRAY;
    type_array_bigint.children.resize(1);
    type_array_bigint.children[0].type = TYPE_BIGINT;
    type_array_bigint.children[0].len = -1;
    auto result = ColumnHelper::create_column(type_array_bigint, true);
    RETURN_IF_ERROR(calc_crop_impl(columns, true, result));
    return result;
}

StatusOr<ColumnPtr> CelonisArrayFunctions::calc_crop_to_null([[maybe_unused]] FunctionContext* context,
                                                             const Columns& columns) {
    DCHECK(columns.size() == 5);
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    auto result = NullableColumn::wrap_if_necessary(columns[0]->clone_empty());
    RETURN_IF_ERROR(calc_crop_impl(columns, false, result));
    return result;
}

StatusOr<ColumnPtr> CelonisArrayFunctions::array_count([[maybe_unused]] FunctionContext* context,
                                                       const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const size_t n_rows = columns[0]->size();
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& offsets = array_data.offsets->get_data().data();
    ColumnBuilder<TYPE_BIGINT> result(n_rows);

    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        int64_t cnt = 0;
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                continue;
            }
            ++cnt;
        }
        result.append(cnt);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisArrayFunctions::array_bool_or([[maybe_unused]] FunctionContext* context,
                                                         const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const size_t n_rows = columns[0]->size();
    ColumnPtr boolean_array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData boolean_array_data = prepare_array_input(boolean_array_column.get());
    const auto& booleans =
            down_cast<const RunTimeColumnType<TYPE_BOOLEAN>&>(*boolean_array_data.elements).get_data().data();
    const auto& offsets = boolean_array_data.offsets->get_data().data();
    ColumnBuilder<TYPE_BOOLEAN> result(n_rows);

    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        bool is_true = false;
        for (auto i = start; i < end; ++i) {
            if (boolean_array_data.null_elements != nullptr && (*boolean_array_data.null_elements)[i] != 0) {
                continue;
            }
            if (booleans[i]) {
                is_true = true;
                break;
            }
        }
        result.append(is_true);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
