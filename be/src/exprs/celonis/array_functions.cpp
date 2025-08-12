#include "exprs/celonis/array_functions.h"

#include <utility>

#include "column/array_column.h"
#include "exprs/celonis/util.h"

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

StatusOr<ColumnPtr> CelonisArrayFunctions::null_to_empty([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    const Column* array = columns[0].get();
    UnnestedArrayData array_data = prepare_array_input(array);
    if (array_data.null_arrays == nullptr) {
        return columns[0];
    }
    auto offsets_ptr = array_data.offsets->get_data().data();
    const auto& array_column = extract_array_column(array);

    auto result_array = ArrayColumn::create(array_column);
    UInt32Column::Container& result_offsets = result_array->offsets_column()->get_data();
    result_offsets.clear();
    const size_t num_array = array_data.offsets->size() - 1;
    int new_offset = 0;
    result_offsets.push_back(new_offset);
    for (size_t i = 0; i < num_array; i++) {
        if ((*array_data.null_arrays)[i]) {
            result_offsets.push_back(new_offset);
        } else {
            new_offset = offsets_ptr[i + 1];
            result_offsets.push_back(new_offset);
        }
    }
    return result_array;
}

} // namespace starrocks
