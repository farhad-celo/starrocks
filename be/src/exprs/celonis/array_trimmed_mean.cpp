#include "array_trimmed_mean.h"

#include <column/column_viewer.h>

#include <execution>

#include "column/column_builder.h"
#include "column/array_column.h"
#include "util.h"


namespace starrocks {
template<LogicalType LT>
StatusOr<ColumnPtr> CelonisArrayTrimmedMean<LT>::celonis_array_trimmed_mean(FunctionContext* ctx,
                                                                            const Columns& columns) {
    using CppType = RunTimeCppType<LT>;
    DCHECK_EQ(3, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    auto [all_const, n_rows] = ColumnHelper::num_packed_rows(columns);

    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();
    const auto& null_elements = array_data.null_elements;

    ColumnViewer lower_cutoff_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer upper_cutoff_viewer = ColumnViewer<TYPE_BIGINT>(columns[2]);

    ColumnBuilder<TYPE_DOUBLE> result_column{static_cast<int32_t>(n_rows)};
    for (auto i{0}; i < n_rows; i++) {
        if (columns[0]->is_null(i) || lower_cutoff_viewer.is_null(i) || upper_cutoff_viewer.is_null(i)) {
            result_column.append_null();
            continue;
        }
        int64_t lower_cutoff = lower_cutoff_viewer.value(i);
        int64_t upper_cutoff = upper_cutoff_viewer.value(i);
        if (lower_cutoff < 0 || lower_cutoff > 100 || upper_cutoff < 0 || upper_cutoff > 100) {
            result_column.append(0.0);

        }
        const auto start = offsets[i];
        const auto end = offsets[i + 1];
        std::vector<CppType> new_vector{};
        size_t skipped_values{0};
        for (auto j = start; j < end; ++j) {
            if (null_elements == nullptr || (*null_elements)[j] == 0) {
                new_vector.push_back(elements[j]);
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
    return result_column.build(all_const);
}

template
class CelonisArrayTrimmedMean<TYPE_INT>;

template
class CelonisArrayTrimmedMean<TYPE_BIGINT>;

template
class CelonisArrayTrimmedMean<TYPE_DOUBLE>;
}