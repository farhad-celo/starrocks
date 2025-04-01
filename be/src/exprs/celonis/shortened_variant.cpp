#include "exprs/celonis/shortened_variant.h"

#include "column/array_column.h"
#include "column/column_viewer.h"
#include "exprs/celonis/util.h"

namespace starrocks {
namespace {
ColumnPtr celonis_shortened_variant_impl(const Column& elements,
                                         const UInt32Column& offsets,
                                         const NullColumn::Container* null_element_offsets,
                                         const NullColumn::Container* null_array_offsets, int64_t length_size) {

    const size_t num_array = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(elements.clone_empty(), NullColumn::create()),
                                            UInt32Column::create());
    UInt32Column::Container& result_offsets = result_array->offsets_column()->get_data();
    ColumnPtr& result_elements = result_array->elements_column();
    using ValueType = RunTimeCppType<TYPE_VARCHAR>;
    auto elements_ptr = (const ValueType *) (elements.raw_data());

    result_offsets.reserve(num_array);
    std::vector<uint32_t> src_index;
    src_index.reserve(elements.size() / 2);
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

        if (array_size == 1) {
            if (null_element_offsets == nullptr || (*null_element_offsets)[offset] == 0) {
                // This is a non-NULL element.
                src_index.push_back(offset);
                ++new_offset;
            }
            result_offsets.push_back(new_offset);
            continue;
        }

        int current_cycle_len = 1;
        auto prev = offset;
        auto cur = offset;
        if (null_element_offsets == nullptr || (*null_element_offsets)[offset] == 0) {
            // This is a non-NULL element.
            src_index.push_back(cur);
            new_offset++;
        }
        while (cur + 1 - offset < array_size) {
            cur++;
            if (null_element_offsets != nullptr && (*null_element_offsets)[cur] != 0) {
                continue;
            }
            if (elements_ptr[cur] == elements_ptr[prev]) {
                current_cycle_len++;
                if (current_cycle_len > length_size) {
                    continue;
                }
            } else {
                current_cycle_len = 1;
                prev = cur;
            }
            src_index.push_back(cur);
            new_offset++;
        }
        result_offsets.push_back(new_offset);

    }
    result_elements->append_selective(elements, src_index);
    return result_array;
}
}  // namespace

StatusOr<ColumnPtr> CelonisShortenedVariant::celonis_shortened_variant(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    const Column* array = columns[0].get();
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());

    ColumnViewer<TYPE_BIGINT> cycle_length(columns[1]);
    ColumnPtr result = celonis_shortened_variant_impl(*array_data.elements, *array_data.offsets,
                                                      array_data.null_elements, array_data.null_arrays,
                                                      cycle_length.value(0));
    if (array_data.null_arrays != nullptr) {
        return NullableColumn::create(std::move(result), down_cast<const NullableColumn*>(array)->null_column());
   }
    return result;
}
} // namespace starrocks
