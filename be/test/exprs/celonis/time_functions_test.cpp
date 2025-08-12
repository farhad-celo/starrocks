#include "exprs/celonis/time_functions.h"

#include "column/column_helper.h"
#include "types/logical_type.h"
#include "types/timestamp_value.h"
#include "util.h"

#include <gtest/gtest.h>

namespace starrocks {

TEST(CelonisTimeFunctionsTest, timestamp_millis) {
    auto col = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    col->append_datum(0L);  // 1970-01-01 00:00:00.000 UTC
    col->append_datum(123L);   // 1970-01-01 00:00:00.123 UTC
    col->append_datum(Datum());  // NULL
    col->append_datum(-1L);  // NULL
    col->append_datum(61123L);    // 1970-01-01 00:01:01.123 UTC

    const auto result = CelonisTimeFunctions::timestamp_millis(nullptr, {col}).value();
    ASSERT_EQ(result->size(), col->size());
    EXPECT_EQ("1970-01-01 00:00:00", result->get(0).get_timestamp().to_string());
    EXPECT_EQ("1970-01-01 00:00:00.123000", result->get(1).get_timestamp().to_string());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ("1970-01-01 00:01:01.123000", result->get(4).get_timestamp().to_string());
}

} // namespace starrocks
