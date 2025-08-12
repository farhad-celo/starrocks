#include "exprs/celonis/index_activity_order.h"

#include "column/array_column.h"
#include "column/column_hash.h"
#include "column/column.h"
#include "exprs/celonis/util.h"

namespace starrocks {

namespace {
ColumnPtr index_activity_order_impl(const Column& elements,
                                    const UInt32Column& offsets,
                                    const NullColumn::Container* null_element_offsets,
                                    const NullColumn::Container* null_array_offsets) {
    const size_t num_rows = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(Int64Column::create(), NullColumn::create()),
                                            UInt32Column::create(offsets));
    ColumnPtr& result_elements = result_array->elements_column();

    result_elements->reserve(elements.size());
    for (size_t i = 0; i < num_rows; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if ((null_array_offsets != nullptr && (*null_array_offsets)[i]) || array_size == 0) {
            continue;
        }

        int64_t idx = 1;
        for (int j = 0; j < array_size; ++j) {
            if (null_element_offsets != nullptr && (*null_element_offsets)[offset + j] != 0) {
                result_elements->append_nulls(1);
            } else {
                result_elements->append_datum(Datum(idx++));
            }
        }
    }
    return result_array;
}

}  // namespace

StatusOr<ColumnPtr> CelonisIndexActivityOrder::celonis_index_activity_order(FunctionContext* context, const Columns& columns) {
    const Column* array = columns[0].get();
    const NullableColumn* nullable_array = nullptr;

    const NullColumn::Container* null_arrays = nullptr;
    if (array->is_nullable()) {
        nullable_array = down_cast<const NullableColumn*>(array);
        array = nullable_array->data_column().get();
        null_arrays = &(nullable_array->null_column()->get_data());
    }

    const auto& array_column = extract_array_column(array);
    const UInt32Column& offsets = array_column.offsets();
    const Column* elements = &array_column.elements();
    const NullColumn::Container* null_elements = nullptr;

    // Indicates that the column has actual NULLs (a column can be Nullable and have no NULL elements).
    bool has_null = elements->has_null();
    if (has_null) {
        null_elements = &(down_cast<const NullableColumn*>(elements)->null_column()->get_data());
    }

    elements = dynamic_cast<const NullableColumn*>(elements)->data_column().get();

    ColumnPtr result = index_activity_order_impl(*elements, offsets, null_elements, null_arrays);
    if (nullable_array != nullptr) {
        return NullableColumn::create(std::move(result), nullable_array->null_column());
    }
    return result;
}

} // namespace starrocks
