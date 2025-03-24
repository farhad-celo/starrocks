#include "exprs/celonis/array_avg.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "exprs/celonis/util.h"

namespace starrocks {

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisArrayAvg<LT>::array_avg([[maybe_unused]] starrocks::FunctionContext* context,
                               const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();
    const auto& null_elements = array_data.null_elements;
    const bool has_null_elements = null_elements != nullptr;
    ColumnBuilder<TYPE_DOUBLE> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        double sum = 0.0;
        int64_t cnt = 0L;
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        if (!has_null_elements) {
            sum = std::accumulate(elements + start, elements + end, 0.0);
            cnt = end - start;
        } else {
            for (auto i = start; i < end; ++i) {
                if ((*null_elements)[i] != 0) {
                    continue;
                }
                ++cnt;
                sum += elements[i];
            }
        }
        if (cnt == 0) {
            result.append_null();
        } else {
            result.append(sum / cnt);
        }
    }
    return result.build(all_const);
}

template
class CelonisArrayAvg<TYPE_INT>;

template
class CelonisArrayAvg<TYPE_BIGINT>;

template
class CelonisArrayAvg<TYPE_DOUBLE>;

} // namespace starrocks
