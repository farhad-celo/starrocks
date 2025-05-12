#include "array_trimmed_mean.h"

#include <column/column_viewer.h>

#include <execution>

#include "column/column_builder.h"
#include "column/array_column.h"
#include "util.h"


namespace starrocks {

template<LogicalType LT>
struct ParallelExecutionThreshold {
    static constexpr int64_t value = 1000;
};

/*
Benchmarking std::accumulate vs std::reduce(std::execution::par_unseq)
Running 10 iterations for each size
=== Benchmarking for int type ===
        Size accumulate (us)     reduce (us)         Speedup
------------------------------------------------------------
         100            0.00           52.10            0.00x
        1000            0.00           15.40            0.00x
       10000            0.50           25.70            0.02x
       15000            1.00           54.50            0.02x
       20000            1.00           50.10            0.02x
      100000            7.10           57.60            0.12x
      200000           15.10           64.70            0.23x
      400000           31.80           80.50            0.40x
      800000           64.10          105.70            0.61x
     1000000           80.00          118.90            0.67x
    10000000         1725.10          651.40            2.65x
*/

template<>
struct ParallelExecutionThreshold<TYPE_INT> {
    static constexpr int64_t value = 10'000'000;
};

/*
=== Benchmarking for int64_t type ===
        Size accumulate (us)     reduce (us)         Speedup
------------------------------------------------------------
         100            0.00            2.10            0.00x
        1000            0.00           14.10            0.00x
       10000            6.90           36.70            0.19x
       15000            8.20           38.10            0.22x
       20000            6.00           54.20            0.11x
      100000           30.90           67.20            0.46x
      200000           65.20           74.20            0.88x
      400000          126.70          100.00            1.27x
      800000          260.60          150.40            1.73x
     1000000          316.20          168.70            1.87x
    10000000         3901.20         1132.10            3.45x
*/

template<>
struct ParallelExecutionThreshold<TYPE_BIGINT> {
    static constexpr int64_t value = 400'000;
};

/*
=== Benchmarking for double type ===
        Size accumulate (us)     reduce (us)         Speedup
------------------------------------------------------------
         100            0.00            6.80            0.00x
        1000            0.00           15.30            0.00x
       10000            9.00           50.80            0.18x
       15000           13.10           50.30            0.26x
       20000           18.00           54.30            0.33x
      100000           92.00           70.10            1.31x
      200000          187.00           93.50            2.00x
      400000          374.50          114.00            3.29x
      800000          753.20          172.70            4.36x
     1000000          938.00          199.40            4.70x
    10000000         9427.80         1156.60            8.15x
*/
template<>
struct ParallelExecutionThreshold<TYPE_DOUBLE> {
    static constexpr int64_t value = 100'000;
};

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

    const int64_t parallel_threshold = ParallelExecutionThreshold<LT>::value;

    std::vector<CppType> buffer;
    for (auto i = 0; i < n_rows; i++) {
        if (columns[0]->is_null(i) || lower_cutoff_viewer.is_null(i) || upper_cutoff_viewer.is_null(i)) {
            result_column.append_null();
            continue;
        }

        const int64_t lower_cutoff = lower_cutoff_viewer.value(i);
        const int64_t upper_cutoff = upper_cutoff_viewer.value(i);
        if (lower_cutoff < 0 || lower_cutoff > 100 || upper_cutoff < 0 || upper_cutoff > 100) {
            return Status::InvalidArgument("CELONIS_ARRAY_TRIMMED_MEAN: Cutoff value must be in interval [0, 100].");
        }
        if (lower_cutoff + upper_cutoff > 100) {
            return Status::InvalidArgument(
                    "CELONIS_ARRAY_TRIMMED_MEAN: Sum of lower cutoff and upper cutoff must be in interval [0, 100].");
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
        if (first > 0) {
            std::nth_element(buffer.begin(), buffer.begin() + first, buffer.end());
        }
        if (last < buffer.size()) {
            std::nth_element(buffer.begin() + first, buffer.begin() + last, buffer.end());
        }
        CppType sum;
        // Use parallel execution only for large arrays (> parallel_threshold)
        if (count > parallel_threshold) {
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