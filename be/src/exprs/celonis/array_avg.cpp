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
    const size_t n_rows = columns[0]->size();
    UnnestedArrayData array_data = prepare_array_input(
            ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]).get());
    const auto& elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_DOUBLE> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        double total = 0.0;
        int64_t cnt = 0L;
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                continue;
            }
            ++cnt;
            total += elements[i];
        }
        if (cnt == 0) {
            result.append_null();
        } else {
            result.append(total / cnt);
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

template
class CelonisArrayAvg<TYPE_INT>;

template
class CelonisArrayAvg<TYPE_BIGINT>;

template
class CelonisArrayAvg<TYPE_DOUBLE>;

} // namespace starrocks
