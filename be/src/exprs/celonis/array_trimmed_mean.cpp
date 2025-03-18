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

    ColumnViewer<TYPE_BIGINT> lower_cutoff_viewer(columns[1]);
    ColumnViewer<TYPE_BIGINT> upper_cutoff_viewer(columns[2]);
    ColumnBuilder<TYPE_DOUBLE> result_column{static_cast<int32_t>(n_rows)};

    std::vector<CppType> buffer;
    for (auto i = 0; i < n_rows; i++) {
        if (columns[0]->is_null(i) || lower_cutoff_viewer.is_null(i) || upper_cutoff_viewer.is_null(i)) {
            result_column.append_null();
            continue;
        }

        const int64_t lower_cutoff = lower_cutoff_viewer.value(i);
        const int64_t upper_cutoff = upper_cutoff_viewer.value(i);
        if (lower_cutoff < 0 || lower_cutoff > 100 || upper_cutoff < 0 || upper_cutoff > 100) {
            result_column.append(0.0);
            continue;
        }

        const auto start = offsets[i];
        const auto end = offsets[i + 1];
        // Reserve buffer space
        buffer.clear();
        size_t skipped_values = 0;
        if (null_elements == nullptr) {
            buffer.assign(elements + start, elements + end);
        } else {
            for (auto j = start; j < end; ++j) {
                if ((*null_elements)[j] == 0) {
                    buffer.push_back(elements[j]);
                } else {
                    skipped_values++;
                }
            }
        }

        // Calculate trim boundaries
        const size_t first = buffer.size() * lower_cutoff / 100;
        const size_t last = buffer.size() - (buffer.size() * upper_cutoff / 100);

        if (first >= last && skipped_values == 0) {
            result_column.append(0.0);
            continue;
        }
        const auto count = static_cast<int64_t>(last) - static_cast<int64_t>(first);
        if (buffer.empty() || count <= 0) {
            result_column.append_null();
            continue;
        }
        std::sort(buffer.begin(), buffer.end());
        CppType sum;
        // Use parallel execution only for large arrays (>1000 elements)
        // TODO(y.zhang): Tune the threshold.
        if (count > 1000) {
            sum = std::reduce(std::execution::par_unseq,
                              buffer.begin() + first,
                              buffer.begin() + last);
        } else {
            // Sequential sum for small arrays to avoid parallel overhead
            sum = std::accumulate(buffer.begin() + first,
                                  buffer.begin() + last,
                                  static_cast<CppType>(0));
        }
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