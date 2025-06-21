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
// 1970-01-01 is a Thursday.
static int post_epoch_weekday[] = {0, 1, 2, 2, 2, 3, 4};
static int pre_epoch_weekday[] = {0, 1, 2, 3, 3, 3, 4};

// Number of days since the start of the unix epoch.
static constexpr JulianDate UNIX_EPOCH_JULIAN = 2440588;

static int64_t convert_timestamp_to_weekday(long timestamp_val) {
    JulianDate year_in_days = timestamp::to_julian(timestamp_val);
    int64_t days_from_unix_epoch = year_in_days - UNIX_EPOCH_JULIAN;
    if (days_from_unix_epoch >= 0) {
        return (days_from_unix_epoch / 7) * 5 + post_epoch_weekday[days_from_unix_epoch % 7];
    }
    days_from_unix_epoch = -days_from_unix_epoch;
    return -((days_from_unix_epoch / 7) * 5 + pre_epoch_weekday[days_from_unix_epoch % 7]);
}
}

StatusOr<ColumnPtr>
CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    ColumnViewer<TYPE_DATETIME> viewer(columns[0]);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (viewer.is_null(row)) {
            result.append_null();
        } else {
            const long timestamp_val = viewer.value(row).timestamp();
            result.append(convert_timestamp_to_weekday(timestamp_val));
        }
    }
    return result.build(all_const);
}

} // namespace starrocks
