#include "exprs/celonis/time_functions.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"

namespace starrocks {

namespace {
static const int64_t NANOS_PER_MILLIS = 1000000;
}

StatusOr<ColumnPtr> CelonisTimeFunctions::timestamp_millis(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);

    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    ColumnViewer<TYPE_BIGINT> data_column(columns[0]);

    auto size = columns[0]->size();
    ColumnBuilder<TYPE_DATETIME> result(size);
    for (int row = 0; row < size; ++row) {
        if (data_column.is_null(row)) {
            result.append_null();
            continue;
        }

        auto unix_millis = data_column.value(row);
        if (unix_millis < 0) {
            result.append_null();
            continue;
        }

        int64 seconds = unix_millis / 1000;
        int64 millis = unix_millis % 1000;
        int64 nanoseconds = millis * NANOS_PER_MILLIS;
        TimestampValue ts;
        ts.set_timestamp(timestamp::of_epoch_second(seconds, nanoseconds));
        result.append(ts);
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
