#include "exprs/celonis/util.h"

#include "column/array_column.h"
#include "column/column_helper.h"

namespace starrocks {
const ArrayColumn& extract_array_column(const Column* input_column) {
    return *(down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_column)));
}

UnnestedArrayData prepare_array_input(const Column* input_array) {
    UnnestedArrayData result;
    const NullableColumn* nullable_array = nullptr;
    if (input_array->is_nullable()) {
        nullable_array = down_cast<const NullableColumn*>(input_array);
        input_array = nullable_array->data_column().get();
        result.null_arrays = &(nullable_array->null_column()->get_data());
    }
    const auto& array_column = extract_array_column(input_array);
    result.offsets = &array_column.offsets();
    result.elements = &array_column.elements();

    // Indicates that the column has actual NULLs (a column can be Nullable and have no NULL elements).
    bool has_null = result.elements->has_null();
    if (has_null) {
        result.null_elements = &(down_cast<const NullableColumn*>(result.elements)->null_column()->get_data());
    }
    if (auto nullable = dynamic_cast<const NullableColumn*>(result.elements); nullable != nullptr) {
        result.elements = nullable->data_column().get();
    }
    return result;
}
} // namespace starrocks
