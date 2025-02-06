#include "array_trimmed_mean.h"

#include <column/column_viewer.h>

#include <execution>

#include "column/column_builder.h"
#include "column/array_column.h"
#include "util.h"


namespace starrocks {
template <LogicalType LT>
StatusOr<ColumnPtr> CelonisArrayTrimmedMean<LT>::celonis_array_trimmed_mean(FunctionContext* ctx,
                                                                            const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[1]);
    ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[2]);

    const auto [elements, offsets, _, null_elements]{prepare_array_input(columns[0].get())};

    ColumnViewer lower_cutoff_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer upper_cutoff_viewer = ColumnViewer<TYPE_BIGINT>(columns[2]);

    const int32_t row_count{static_cast<int32_t>(offsets->size() - 1)};
    ColumnBuilder<TYPE_DOUBLE> result_column{row_count};

    const auto& offset_data{offsets->get_data()};

    for (int32_t i{0}; i < row_count; i++) {
        int64_t lower_cutoff = lower_cutoff_viewer.value(i);

        int64_t upper_cutoff = upper_cutoff_viewer.value(i);
        if (lower_cutoff < 0 || lower_cutoff > 100 || upper_cutoff < 0 || upper_cutoff > 100) {
            result_column.append(0.0);

        }
        if (columns[0]->is_null(i)) {
            result_column.append_null();
            continue;
        }
        const size_t array_size{offset_data[i + 1] - offset_data[i]};
        using CppType = RunTimeCppType<LT>;
        std::vector<CppType> new_vector{};
        size_t skipped_values{0};
        for (size_t group_index{0}; group_index < array_size; group_index++) {
            if (null_elements == nullptr ||
                !null_elements->data()[offset_data[i] + group_index]) {
                new_vector.push_back(elements->get(
                        offset_data[i] + group_index).get<CppType>());
            } else {
                skipped_values++;
            }
        }
        const size_t first{new_vector.size() * lower_cutoff / 100};
        const size_t last{new_vector.size() - (new_vector.size() * upper_cutoff / 100)};
        // if all values are trimmed and we do not have null values, 0 is returned
        if (first >= last && skipped_values == 0) {
            result_column.append(0.0);
            continue;
        }
        const size_t count{last - first};
        if (new_vector.empty() || count <= 0) {
            result_column.append_null();
            continue;
        }
        std::sort(new_vector.begin(), new_vector.end(), std::less<CppType>());
        const auto sum{std::reduce(std::execution::par_unseq, new_vector.begin() + first,
                                   new_vector.begin() + last)};

        result_column.append(static_cast<double>(sum) / count);
    }
    return result_column.build(false);
}

template
class CelonisArrayTrimmedMean<TYPE_INT>;

template
class CelonisArrayTrimmedMean<TYPE_BIGINT>;

template
class CelonisArrayTrimmedMean<TYPE_DOUBLE>;
}