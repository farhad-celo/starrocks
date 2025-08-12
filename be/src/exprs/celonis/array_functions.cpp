#include "exprs/celonis/array_functions.h"

#include <vector>
#include <utility>

#include "column/array_column.h"
#include "exprs/celonis/util.h"
#include "column/column_viewer.h"

namespace starrocks {

class CelonisArrayIsSortedImpl {
public:
    static ColumnPtr evaluate(const Column& array) {
        return _array_is_sorted_generic(array);
    }

private:
    template <bool NullableElement, typename ElementColumn>
    static ColumnPtr _process(const ElementColumn& elements, const UInt32Column& offsets,
                              const NullColumn::Container* null_map_elements) {
        const size_t num_array = offsets.size() - 1;
        auto result = UInt8Column::create();
        result->resize(num_array);

        auto* result_ptr = result->get_data().data();

        auto offsets_ptr = offsets.get_data().data();
        [[maybe_unused]] auto elements_ptr = (const typename ElementColumn::ValueType*)(elements.raw_data());

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

    template <bool NullableElement>
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


StatusOr<ColumnPtr> CelonisArrayFunctions::array_is_sorted([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    const ColumnPtr& arg0 = columns[0]; // array

    return CelonisArrayIsSortedImpl::evaluate(*arg0);
}

class CelonisMergeSortedArrays {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 4);

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

        for (size_t row = 0; row < chunk_size; row++) {
            size_t src_timestamp_start = src_offsets[row];
            size_t src_timestamp_end = src_offsets[row + 1];
            if (timestamp_offsets[row + 1] != src_timestamp_end) {
                return Status::InvalidArgument("The size of input_array and timestamp_array should not be different.");
            }
            size_t size_priority_start = size_offsets[row];
            size_t size_priority_end = size_offsets[row + 1];
            if (priority_offsets[row + 1] != size_priority_end) {
                return Status::InvalidArgument("The size of size_array and priority_array should not be different.");
            }

            struct Array {
                Array(size_t start, size_t end, const TimestampValue* timestamp, int priority)
                        : index(start), end(end), timestamp(timestamp), priority(priority) {}
                size_t index;
                size_t end;
                const TimestampValue* timestamp;
                int priority;
            };
            struct CompareArrayElement {
                bool operator()(const Array& lhs, const Array& rhs) {
                    return *lhs.timestamp > *rhs.timestamp ||
                           (*lhs.timestamp == *rhs.timestamp && lhs.priority < rhs.priority);
                }
            };
            std::priority_queue<Array, std::vector<Array>, CompareArrayElement> pq;
            size_t start = src_timestamp_start;
            size_t next = 0;
            for (size_t i = size_priority_start; i < size_priority_end; i++) {
                next = start + sizes[i];
                if (next > src_timestamp_end) {
                    return Status::InvalidArgument(
                            "The size of input_array and timestamp_array should not be different than the sum of "
                            "size_array.");
                }
                pq.emplace(start, next, timestamps + start, priorities[i]);
                start = next;
            }
            if (next != src_timestamp_end) {
                return Status::InvalidArgument(
                        "The size of input_array and timestamp_array should not be different than the sum of "
                        "size_array.");
            }
            while (!pq.empty()) {
                Array curr = pq.top();
                pq.pop();
                src_index.push_back(curr.index);
                new_offset++;
                if (++curr.index < curr.end) {
                    curr.timestamp++;
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
            if (src_column->has_null()) {
                return Status::InvalidArgument("input_array should not be null.");
            }
            const auto* src_nullable_column = down_cast<const NullableColumn*>(src_column.get());
            const auto& src_data_column = src_nullable_column->data_column_ref();

            auto* dest_nullable_column = down_cast<NullableColumn*>(dest_column.get());
            auto* dest_data_column = dest_nullable_column->mutable_data_column();
            auto* dest_null_column = dest_nullable_column->mutable_null_column();
            dest_null_column->get_data().resize(chunk_size, 0);
            dest_nullable_column->set_has_null(false);
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
            if (key_array_ptr->has_null()) {
                return Status::InvalidArgument("key_array should not be null.");
            }
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
            if (end != key_offsets[i + 1]) {
                return Status::InvalidArgument("The size of input_array and key_array should not be different.");
            }
            for (auto id = start; id < end; ++id) {
                if (id == start || prev != key_data[id]) {
                    src_index.push_back(id);
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
        DCHECK_EQ(offset_viewer.size(), chunk_size);

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
            if (input_column->has_null()) {
                return Status::InvalidArgument("input_array should not be null.");
            }
            const auto* input_nullable_column = down_cast<const NullableColumn*>(input_column.get());
            const auto& input_data_column = input_nullable_column->data_column_ref();

            auto* output_nullable_column = down_cast<NullableColumn*>(output_column.get());
            auto* output_data_column = output_nullable_column->mutable_data_column();
            auto* output_null_column = output_nullable_column->mutable_null_column();
            output_null_column->get_data().resize(chunk_size, 0);
            output_nullable_column->set_has_null(false);
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

} // namespace starrocks
