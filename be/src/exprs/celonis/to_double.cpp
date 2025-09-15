#include "exprs/celonis/to_double.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "exprs/function_context.h"

namespace starrocks {

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisToDouble<LT>::to_double([[maybe_unused]] starrocks::FunctionContext* context,
                               const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    if constexpr (LT == TYPE_DOUBLE) {
        return columns[0]->clone_shared();
    }
    ColumnViewer value_viewer = ColumnViewer<LT>(columns[0]);

    ColumnBuilder<TYPE_DOUBLE> result(num_rows);

    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        if constexpr (LT == TYPE_DATETIME) {
            result.append(value_viewer.value(row).to_unix_second() * 1000.0);
        } else {
            result.append(value_viewer.value(row));
        }
    }
    return result.build(all_const);
}

template
class CelonisToDouble<TYPE_DOUBLE>;

template
class CelonisToDouble<TYPE_INT>;

template
class CelonisToDouble<TYPE_BIGINT>;

template
class CelonisToDouble<TYPE_DATETIME>;

} // namespace starrocks
