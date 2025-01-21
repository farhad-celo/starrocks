#include "exprs/celonis/remap_timestamp_weekday.h"

#include "column/array_column.h"
#include "column/column.h"
#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_viewer.h"
#include "exprs/celonis/util.h"

namespace starrocks {

namespace {
// See https://docs.google.com/document/d/1q_MDFvi3Y9VP_HlAud7Nf--KXi_uB-FU0OODWO1pgJU/edit#.
static int weekday[] = {0, 1, 2, 2, 2, 3, 4};

// Number of days since the start of the unix epoch.
static constexpr JulianDate UNIX_EPOCH_JULIAN = 2440588;

static int64_t convert_timestamp_to_weekday(long timestamp_val) {
    JulianDate year_in_days = timestamp::to_julian(timestamp_val);
    int64_t days_from_unix_epoch = year_in_days - UNIX_EPOCH_JULIAN;
    return (days_from_unix_epoch / 7) * 5 + weekday[days_from_unix_epoch % 7];
}
}

StatusOr<ColumnPtr>
CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    ColumnViewer<TYPE_DATETIME> viewer(columns[0]);
    size_t size = columns[0]->size();
    ColumnBuilder<TYPE_BIGINT> builder(size);
    for (int row = 0; row < size; ++row) {
        if (viewer.is_null(row)) {
            builder.append_null();
        } else {
            const long timestamp_val = viewer.value(row).timestamp();
            builder.append(convert_timestamp_to_weekday(timestamp_val));
        }
    }
    return builder.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
