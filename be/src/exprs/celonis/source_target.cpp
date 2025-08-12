#include "exprs/celonis/source_target.h"

#include "column/array_column.h"
#include "column/column_viewer.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

template<bool has_null, CelonisSourceTargetFunctions::EdgeConfig format>
ColumnPtr CelonisSourceTargetFunctions::_celonis_array_targets_impl(FunctionContext* context, const Column& elements,
                                                                    const UInt32Column& offsets,
                                                                    const NullColumn::Container* null_element_offsets,
                                                                    const NullColumn::Container* null_array_offsets) {
    const size_t num_array = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(elements.clone_empty(), NullColumn::create()),
                                            UInt32Column::create());
    UInt32Column::Container& result_offsets = result_array->offsets_column()->get_data();
    ColumnPtr& result_elements = result_array->elements_column();

    // TODO(gubichev): revisit this for other edge configs.
    result_offsets.reserve(num_array);
    result_elements->reserve(elements.size());
    size_t new_offset = 0;
    for (size_t i = 0; i < num_array; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if ((null_array_offsets != nullptr && (*null_array_offsets)[i]) || array_size == 0) {
            // If null_array_offset[i] is true, the current array is NULL.
            // If array_size is 0, the current array is empty.
            result_offsets.push_back(new_offset);
            continue;
        }
        result_elements->append(elements, offset + 1, array_size - 1);
        if constexpr (has_null) {
            // Input has nulls, propagate them to output.
            for (int j = 1; j < array_size; ++j) {
                if ((*null_element_offsets)[offset + j] != 0) {
                    auto res = (result_elements->set_null(new_offset + j - 1));
                    DCHECK(res);
                }
            }
        }
        new_offset = new_offset + array_size - 1;
        result_offsets.push_back(new_offset);
    }
    return result_array;
}

template <bool has_null, CelonisSourceTargetFunctions::EdgeConfig format>
ColumnPtr CelonisSourceTargetFunctions::_celonis_array_sources_impl(FunctionContext* context, const Column& elements,
                                                        const UInt32Column& offsets,
                                                        const NullColumn::Container* null_element_offsets,
                                                        const NullColumn::Container* null_array_offsets) {
    const size_t num_array = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(elements.clone_empty(), NullColumn::create()),
                                            UInt32Column::create());
    UInt32Column::Container& result_offsets = result_array->offsets_column()->get_data();
    ColumnPtr& result_elements = result_array->elements_column();

    // TODO(gubichev): revisit this for other edge configs.
    result_offsets.reserve(num_array);
    result_elements->reserve(elements.size());
    size_t new_offset = 0;
    for (size_t i = 0; i < num_array; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if ((null_array_offsets != nullptr && (*null_array_offsets)[i]) || array_size == 0) {
            // If null_array_offset[i] is true, the current array is NULL.
            // If array_size is 0, the current array is empty.
            // TODO(a.gubichev): add another template parameter for nullability of arrays.
            result_offsets.push_back(new_offset);
            continue;
        }
        result_elements->append(elements, offset, array_size - 1);
        if constexpr (has_null) {
            // Input has nulls, propagate them to output.
            for (int j = 0; j < array_size - 1; ++j) {
                if ((*null_element_offsets)[offset + j] != 0) {
                    auto res = (result_elements->set_null(new_offset + j));
                    DCHECK(res);
                }
            }
        }
        new_offset = new_offset + array_size - 1;
        result_offsets.push_back(new_offset);
    }
    return result_array;
}

static CelonisSourceTargetFunctions::EdgeConfig getEdgeConfig(const std::string& format) {
    if (format == "any->any") {
        return CelonisSourceTargetFunctions::ANY_TO_ANY;
    }
    return CelonisSourceTargetFunctions::DEFAULT;
}

namespace {
// Contains the representation of array column.
struct UnnestedArrayData {
    // Flattened array elements
    const Column* elements = nullptr;
    // Offsets (indicating new array start)
    const UInt32Column* offsets = nullptr;
    // Null indicators for NULL arrays
    const NullColumn::Container* null_arrays = nullptr;
    // Null indicators for NULL elements
    const NullColumn::Container* null_elements = nullptr;
};

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

}  // namespace

StatusOr<ColumnPtr> CelonisSourceTargetFunctions::celonis_array_sources(FunctionContext* context, const Columns& columns) {
    const Column* array = columns[0].get();

    UnnestedArrayData array_data = prepare_array_input(array);
    ColumnViewer<TYPE_VARCHAR> viewer_format(columns[1]);
    CelonisSourceTargetFunctions::EdgeConfig edge_config = getEdgeConfig(viewer_format.value(0).to_string());

    if (edge_config != ANY_TO_ANY) {
        // TODO(gubichev): support other edge configurations.
        std::stringstream error;
        error << "unsupported format in celonis_array_sources" << std::endl;
        throw std::runtime_error(error.str());
    }

    ColumnPtr result;
    if (array_data.null_elements != nullptr) {
        result = _celonis_array_sources_impl<true, ANY_TO_ANY>(context, *array_data.elements, *array_data.offsets, array_data.null_elements, array_data.null_arrays);
    } else {
        result = _celonis_array_sources_impl<false, ANY_TO_ANY>(context, *array_data.elements, *array_data.offsets, array_data.null_elements, array_data.null_arrays);
    }
    if (array_data.null_arrays != nullptr) {
        return NullableColumn::create(std::move(result), down_cast<const NullableColumn*>(array)->null_column());
    }
    return result;
}

StatusOr<ColumnPtr> CelonisSourceTargetFunctions::celonis_array_targets(FunctionContext* context, const Columns& columns) {
    const Column* array = columns[0].get();

    UnnestedArrayData array_data = prepare_array_input(array);
    ColumnViewer<TYPE_VARCHAR> viewer_format(columns[1]);
    CelonisSourceTargetFunctions::EdgeConfig edge_config = getEdgeConfig(viewer_format.value(0).to_string());

    if (edge_config != ANY_TO_ANY) {
        // TODO(gubichev): support other edge configurations.
        std::stringstream error;
        error << "unsupported format in celonis_array_sources" << std::endl;
        throw std::runtime_error(error.str());
    }
    ColumnPtr result;
    if (array_data.null_elements != nullptr) {
        result = _celonis_array_targets_impl<true, ANY_TO_ANY>(context, *array_data.elements, *array_data.offsets, array_data.null_elements, array_data.null_arrays);
    } else {
        result = _celonis_array_targets_impl<false, ANY_TO_ANY>(context, *array_data.elements, *array_data.offsets, array_data.null_elements, array_data.null_arrays);
    }
    if (array_data.null_arrays != nullptr) {
        return NullableColumn::create(std::move(result), down_cast<const NullableColumn*>(array)->null_column());
    }
    return result;
}

} // namespace starrocks
