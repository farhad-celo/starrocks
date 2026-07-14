#include "exprs/celonis/time_functions.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "google/protobuf/text_format.h"
#include "testutil/function_utils.h"
#include "types/timestamp_value.h"
#include "util.h"

namespace starrocks {

TEST(TimeRangeTest, weekly_compute_overlap) {
    constexpr int64_t NUM_DAYS_PER_WEEK = 7L;
    constexpr int64_t DAY_MS = 86400000L;
    constexpr int64_t WEEK_MS = DAY_MS * NUM_DAYS_PER_WEEK;

    {
        // Empty range
        constexpr std::array EMPTY_RANGES{
                TimeRange{0, 0, true},
                TimeRange{10, 10, true},
                TimeRange{-10, -10, true},
        };
        for (const auto& range : EMPTY_RANGES) {
            EXPECT_EQ(range.compute_overlap(0, 0), 0);
            EXPECT_EQ(range.compute_overlap(0, 10), 0);
            EXPECT_EQ(range.compute_overlap(-10, 0), 0);
            EXPECT_EQ(range.compute_overlap(-10, 10), 0);
            EXPECT_EQ(range.compute_overlap(1, 0), 0);
            EXPECT_EQ(range.compute_overlap(0, 10 * WEEK_MS), 0);
        }
    }
    {
        // Simple range
        const TimeRange range{0, 10, true};

        // No overlap
        EXPECT_EQ(range.compute_overlap(0, 0), 0);
        EXPECT_EQ(range.compute_overlap(10, 20), 0);
        EXPECT_EQ(range.compute_overlap(-10, 0), 0);
        EXPECT_EQ(range.compute_overlap(-10, -5), 0);
        EXPECT_EQ(range.compute_overlap(1, 0), 0);

        // Full overlap
        EXPECT_EQ(range.compute_overlap(0, 10), 10);
        EXPECT_EQ(range.compute_overlap(0, 20), 10);
        EXPECT_EQ(range.compute_overlap(-10, 10), 10);
        EXPECT_EQ(range.compute_overlap(-10, 20), 10);

        // Partial overlap
        EXPECT_EQ(range.compute_overlap(0, 8), 8);
        EXPECT_EQ(range.compute_overlap(2, 10), 8);
        EXPECT_EQ(range.compute_overlap(2, 8), 6);

        // Full overlap in other week
        EXPECT_EQ(range.compute_overlap(WEEK_MS, WEEK_MS + 10), 10);
        EXPECT_EQ(range.compute_overlap(WEEK_MS, WEEK_MS + 20), 10);
        EXPECT_EQ(range.compute_overlap(17 * WEEK_MS, 17 * WEEK_MS + 10), 10);
        EXPECT_EQ(range.compute_overlap(-WEEK_MS, -WEEK_MS + 10), 10);
        EXPECT_EQ(range.compute_overlap(-WEEK_MS, -WEEK_MS + 20), 10);
        EXPECT_EQ(range.compute_overlap(-WEEK_MS, 0), 10);

        // Partial overlap in other week
        EXPECT_EQ(range.compute_overlap(WEEK_MS, WEEK_MS + 8), 8);
        EXPECT_EQ(range.compute_overlap(WEEK_MS + 2, WEEK_MS + 10), 8);
        EXPECT_EQ(range.compute_overlap(WEEK_MS + 2, WEEK_MS + 8), 6);
        EXPECT_EQ(range.compute_overlap(-17 * WEEK_MS, -17 * WEEK_MS + 8), 8);
        EXPECT_EQ(range.compute_overlap(-17 * WEEK_MS + 2, -17 * WEEK_MS + 10), 8);
        EXPECT_EQ(range.compute_overlap(-17 * WEEK_MS + 2, -17 * WEEK_MS + 8), 6);

        // Repeated overlap
        EXPECT_EQ(range.compute_overlap(0, WEEK_MS + 10), 20);
        EXPECT_EQ(range.compute_overlap(0, 17 * WEEK_MS + 10), 180);
        EXPECT_EQ(range.compute_overlap(3 * WEEK_MS, 17 * WEEK_MS + 10), 150);
        EXPECT_EQ(range.compute_overlap(-17 * WEEK_MS, 17 * WEEK_MS + 10), 350);
        EXPECT_EQ(range.compute_overlap(-17 * WEEK_MS, -3 * WEEK_MS), 140);

        // Repeated & partial overlap
        EXPECT_EQ(range.compute_overlap(0, WEEK_MS + 8), 18);
        EXPECT_EQ(range.compute_overlap(2, WEEK_MS + 10), 18);
        EXPECT_EQ(range.compute_overlap(2, WEEK_MS + 8), 16);
        EXPECT_EQ(range.compute_overlap(3, 17 * WEEK_MS + 5), 7 + 160 + 5);
        EXPECT_EQ(range.compute_overlap(2 * WEEK_MS + 3, 17 * WEEK_MS + 5), 7 + 140 + 5);
        EXPECT_EQ(range.compute_overlap(-17 * WEEK_MS + 3, 17 * WEEK_MS + 5), 7 + 160 + 170 + 5);
        EXPECT_EQ(range.compute_overlap(-17 * WEEK_MS + 3, -3 * WEEK_MS + 5), 7 + 130 + 5);
    }
}

class CelonisTimeFunctionsTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
};

TEST_F(CelonisTimeFunctionsTest, millis_timestamp) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0, 123000));
    timestamps->append_datum(kNullDatum);
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
    timestamps->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
    const auto result = CelonisTimeFunctions::millis_timestamp(nullptr, {timestamps}).value();
    ASSERT_EQ(timestamps->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(123L, result->get(1).get_int64());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ(3600000L, result->get(3).get_int64());
    EXPECT_EQ(-86400000L, result->get(4).get_int64());
}

TEST_F(CelonisTimeFunctionsTest, millis_timestamp_empty_input) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
    const auto result = CelonisTimeFunctions::millis_timestamp(nullptr, {timestamps}).value();
    ASSERT_EQ(timestamps->size(), result->size());
}

TEST_F(CelonisTimeFunctionsTest, timestamp_millis) {
    auto col = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    col->append_datum(0L);               // 1970-01-01 00:00:00.000 UTC
    col->append_datum(123L);             // 1970-01-01 00:00:00.123 UTC
    col->append_datum(Datum());          // NULL
    col->append_datum(-11676096000000L); // 1600-01-01 00:00:00
    col->append_datum(61123L);           // 1970-01-01 00:01:01.123 UTC
    col->append_datum(172800000L);       // 1970-01-03 00:00:00
    col->append_datum(-11676182400000L); // 1599-12-31 00:00:00
    col->append_datum(-1L);              // 1969-12-31 23:59:59.999 UTC
    col->append_datum(-999L);            // 1969-12-31 23:59:59.001 UTC
    col->append_datum(-1001L);           // 1969-12-31 23:59:58.999 UTC

    const auto result = CelonisTimeFunctions::timestamp_millis(nullptr, {col}).value();
    ASSERT_EQ(result->size(), col->size());
    EXPECT_EQ(result->get(0).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0));
    EXPECT_EQ(result->get(1).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0, 123000));
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ(result->get(3).get_timestamp(), TimestampValue::create(1600, 1, 1, 0, 0, 0, 0));
    EXPECT_EQ(result->get(4).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 1, 1, 123000));
    EXPECT_EQ(result->get(5).get_timestamp(), TimestampValue::create(1970, 1, 3, 0, 0, 0));
    EXPECT_EQ(result->get(6).get_timestamp(), TimestampValue::create(1599, 12, 31, 0, 0, 0, 0));
    EXPECT_EQ(result->get(7).get_timestamp(), TimestampValue::create(1969, 12, 31, 23, 59, 59, 999000));
    EXPECT_EQ(result->get(8).get_timestamp(), TimestampValue::create(1969, 12, 31, 23, 59, 59, 1000));
    EXPECT_EQ(result->get(9).get_timestamp(), TimestampValue::create(1969, 12, 31, 23, 59, 58, 999000));
}

TEST_F(CelonisTimeFunctionsTest, timestamp_to_millis_precision) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0, 123456));
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0, 999));
    timestamps->append_datum(kNullDatum);
    timestamps->append_datum(TimestampValue::create(1969, 12, 31, 23, 59, 59, 999999));

    const auto result = CelonisTimeFunctions::timestamp_to_millis_precision(nullptr, {timestamps}).value();
    ASSERT_EQ(timestamps->size(), result->size());
    EXPECT_EQ(result->get(0).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0, 123000));
    EXPECT_EQ(result->get(1).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0));
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ(result->get(3).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0));
}

TEST_F(CelonisTimeFunctionsTest, normalize_timestamp) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
    timestamps->append_datum(TimestampValue::create(1399, 12, 31, 23, 59, 59, 999999));
    timestamps->append_datum(TimestampValue::create(1400, 1, 1, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0, 123456));
    timestamps->append_datum(TimestampValue::create(1969, 12, 31, 23, 59, 59, 999999));
    timestamps->append_datum(TimestampValue::create(1969, 12, 31, 23, 59, 59, 999000));
    timestamps->append_datum(TimestampValue::create(9999, 12, 31, 23, 59, 59, 999999));
    timestamps->append_datum(TimestampValue::create(10000, 1, 1, 0, 0, 0));
    timestamps->append_datum(kNullDatum);

    const auto result = CelonisTimeFunctions::normalize_timestamp(nullptr, {timestamps}).value();
    ASSERT_EQ(result->size(), timestamps->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(result->get(1).get_timestamp(), TimestampValue::create(1400, 1, 1, 0, 0, 0));
    EXPECT_EQ(result->get(2).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0, 123000));
    EXPECT_EQ(result->get(3).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0));
    EXPECT_EQ(result->get(4).get_timestamp(), TimestampValue::create(1969, 12, 31, 23, 59, 59, 999000));
    EXPECT_EQ(result->get(5).get_timestamp(), TimestampValue::create(9999, 12, 31, 23, 59, 59, 999000));
    EXPECT_TRUE(result->get(6).is_null());
    EXPECT_TRUE(result->get(7).is_null());
}

TEST_F(CelonisTimeFunctionsTest, date_between) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto begin_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto end_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 2, 1, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 3, 1, 0, 0));
    for (auto i = 0; i < timestamps->size(); ++i) {
        begin_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        end_timestamps->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
    }
    const auto result =
            CelonisTimeFunctions::date_between(nullptr, {timestamps, begin_timestamps, end_timestamps}).value();
    ASSERT_EQ(timestamps->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

TEST_F(CelonisTimeFunctionsTest, date_between_null_input) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto begin_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto end_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        timestamps->append_datum(kNullDatum);
        begin_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        end_timestamps->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
        const auto result =
                CelonisTimeFunctions::date_between(nullptr, {timestamps, begin_timestamps, end_timestamps}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto begin_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto end_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        begin_timestamps->append_datum(kNullDatum);
        end_timestamps->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
        const auto result =
                CelonisTimeFunctions::date_between(nullptr, {timestamps, begin_timestamps, end_timestamps}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto begin_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto end_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        begin_timestamps->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
        end_timestamps->append_datum(kNullDatum);
        const auto result =
                CelonisTimeFunctions::date_between(nullptr, {timestamps, begin_timestamps, end_timestamps}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTimeFunctionsTest, date_between_empty_input) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto begin_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto end_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    const auto result =
            CelonisTimeFunctions::date_between(nullptr, {timestamps, begin_timestamps, end_timestamps}).value();
    EXPECT_EQ(0, result->size());
}

} // namespace starrocks
