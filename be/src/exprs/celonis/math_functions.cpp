#include "exprs/celonis/math_functions.h"

#include "column/array_column.h"
#include "column/column_viewer.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"

namespace starrocks {

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisMathFunctions<LT>::square([[maybe_unused]] starrocks::FunctionContext* context,
                                 const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    ColumnViewer value_viewer = ColumnViewer<LT>(columns[0]);

    const size_t n_rows = columns[0]->size();
    ColumnBuilder<LT> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        result.append(value_viewer.value(row) * value_viewer.value(row));
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

template
class CelonisMathFunctions<TYPE_INT>;

template
class CelonisMathFunctions<TYPE_BIGINT>;

template
class CelonisMathFunctions<TYPE_DOUBLE>;

} // namespace starrocks
