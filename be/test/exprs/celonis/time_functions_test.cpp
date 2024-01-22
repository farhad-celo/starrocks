#include "exprs/celonis/time_functions.h"

#include "column/column_helper.h"
#include "types/timestamp_value.h"
#include "util.h"
#include <gtest/gtest.h>

namespace starrocks {

class CelonisTimeFunctionsTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
};

TEST_F(CelonisTimeFunctionsTest, timestamp_millis) {
    auto col = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    col->append_datum(0L);  // 1970-01-01 00:00:00.000 UTC
    col->append_datum(123L);   // 1970-01-01 00:00:00.123 UTC
    col->append_datum(Datum());  // NULL
    col->append_datum(-1L);  // NULL
    col->append_datum(61123L);    // 1970-01-01 00:01:01.123 UTC
    col->append_datum(172800000L);  // 1970-01-03 00:00:00

    const auto result = CelonisTimeFunctions::timestamp_millis(nullptr, {col}).value();
    ASSERT_EQ(result->size(), col->size());
    EXPECT_EQ(result->get(0).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0));
    EXPECT_EQ(result->get(1).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0, 123000));
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(result->get(4).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 1, 1, 123000));
    EXPECT_EQ(result->get(5).get_timestamp(), TimestampValue::create(1970, 1, 3, 0, 0, 0));
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_without_calendar) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto expected_output = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);

    std::vector<DatumStruct> test_sets = {
            {TimestampValue::create(1970, 1, 1, 0, 0, 0),   "MILLISECONDS", 0L},
            {TimestampValue::create(1970, 1, 1, 1, 0, 0),   "MILLISECONDS", 3600000L},
            {TimestampValue::create(1970, 1, 1, 0, 0, 10),  "MILLISECONDS", 10000L},
            {TimestampValue::create(1970, 1, 1, 0, 0, 0),   "SECONDS",      0L},
            {TimestampValue::create(1970, 1, 2, 0, 0, 0),   "SECONDS",      86400L},
            {TimestampValue::create(1969, 12, 31, 0, 0, 0), "SECONDS",      -86400L},
            {TimestampValue::create(1970, 1, 1, 0, 0, 0),   "MINUTES",      0L},
            {TimestampValue::create(1970, 2, 1, 0, 0, 0),   "MINUTES",      44640L},
            {TimestampValue::create(1971, 1, 1, 0, 0, 0),   "HOURS",        8760L},
            {TimestampValue::create(1972, 1, 1, 0, 0, 0),   "HOURS",        17520L},
            {TimestampValue::create(1975, 1, 1, 0, 0, 0),   "DAYS",         1826L},

    };

    for (const auto& test_set: test_sets) {
        timestamps->append_datum(test_set[0]);
        time_units->append_datum(test_set[1]);
        calendars->append_datum(DatumArray());
        calendar_ids->append_datum(kNullDatum);
        expected_output->append_datum(test_set[2]);
    }

    const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                  calendar_ids}).value();
    ASSERT_EQ(timestamps->size(), result->size());
    ASSERT_EQ(test_sets.size(), result->size());
    for (size_t i = 0; i < result->size(); ++i) {
        EXPECT_EQ(expected_output->get(i).get_int64(), result->get(i).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_weekday_calendar) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [0, 1000 milliseconds]
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm] = 9 hours
                R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // use_day = false
                R"("friday": {"use_day": false, "shift": {"begin": 28800000, "end": 61200000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm] = 9 hours
                R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"("friday": {"use_day": false, "shift": {"begin": 28800000, "end": 61200000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        time_units->append_datum("MINUTES");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] & [12:00 am, 10:00 am] = 120 mins
                R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(660L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 8, 0, 0, 0));
        time_units->append_datum("MINUTES");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("wednesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"("friday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                R"("saturday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                R"("sunday": {"use_day": false, "shift": {"begin": 0, "end": 0} } )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(2160L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 8, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 9:00 am] = 1h
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 32400000} }, )",
                // [8:00 am, 10:00 am] = 2h
                R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 36000000} }, )",
                // [8:00 am, 11:00 am] = 3h
                R"("wednesday": {"use_day": true, "shift": {"begin": 28800000, "end": 39600000} }, )",
                // [8:00 am, 12:00 pm] = 4h
                R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 43200000} }, )",
                R"("friday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                R"("saturday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                // [9:00 am, 12:00 pm] = 3h
                R"("sunday": {"use_day": true, "shift": {"begin": 32400000, "end": 43200000} } )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(13L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        // Tuesday
        timestamps->append_datum(TimestampValue::create(1970, 1, 13, 10, 0, 0));
        time_units->append_datum("MILLISECONDS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("wednesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"("friday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                R"("saturday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                R"("sunday": {"use_day": false, "shift": {"begin": 0, "end": 0} } )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(201600000L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [12:00 am, 8:00 am]
                R"("wednesday": {"use_day": true, "shift": {"begin": 0, "end": 28800000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-28800L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [12:00 am, 8:00 am]
                R"("wednesday": {"use_day": true, "shift": {"begin": 0, "end": 28800000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-8L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 12, 30, 20, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm]
                R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [12:00 am, 8:00 am]
                R"("wednesday": {"use_day": true, "shift": {"begin": 0, "end": 28800000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-8L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        // Tuesday
        timestamps->append_datum(TimestampValue::create(1969, 1, 1, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("wednesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                // [8:00 am, 5:00 pm] = 540 mins
                R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"("friday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                R"("saturday": {"use_day": false, "shift": {"begin": 0, "end": 0} }, )",
                R"("sunday": {"use_day": false, "shift": {"begin": 0, "end": 0} } )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-1881L, result->get(0).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_weekday_calendar_multi_rows) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
    time_units->append_datum("MINUTES");
    time_units->append_datum("HOURS");
    calendars->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            // [8:00 am, 5:00 pm]
            R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
            // [8:00 am, 5:00 pm]
            R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} })",
            R"(} })"});
    calendars->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            // [8:00 am, 5:00 pm]
            R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
            R"("friday": {"use_day": false, "shift": {"begin": 0, "end": 0} })",
            R"(} })"});
    calendar_ids->append_datum(kNullDatum);
    calendar_ids->append_datum(kNullDatum);
    const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                  calendar_ids}).value();
    ASSERT_EQ(timestamps->size(), result->size());
    EXPECT_EQ(660L, result->get(0).get_int64());
    EXPECT_EQ(9L, result->get(1).get_int64());
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_null_columns) {
    // NULL timestamp
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(kNullDatum);
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray());
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        EXPECT_TRUE(result->is_null(0));
    }
    // NULL time_unit
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum(kNullDatum);
        calendars->append_datum(DatumArray());
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        EXPECT_TRUE(result->is_null(0));
    }
    // NULL calendar_specification
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(kNullDatum);
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        EXPECT_TRUE(result->is_null(0));
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_invalid_time_unit) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    time_units->append_datum("YEARS");
    calendars->append_datum(DatumArray());
    calendar_ids->append_datum(kNullDatum);
    const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                  calendar_ids});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "time unit must be one of DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_malformed_calendar) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar array should not have null elements.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("unknown_day": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar specification column is malformed.");
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_invalid_weekday_calendar) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("monday": {"use_day": true, "shift": {"begin": -1, "end": 1000} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "shift begin is negative in weekday calendar.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("monday": {"use_day": true, "shift": {"begin": 1000, "end": 500} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "shift begin is greater than shift end in weekday calendar.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("monday": {"use_day": true, "shift": {"begin": 1000, "end": 86400005} })",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "shift end is greater than 86400000 milliseconds in weekday calendar.");
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_calendar_id_provided) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("monday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendar_ids->append_datum("CalendarID");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar ID column should not be set for weekday calendar.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("MILLISECONDS");
        calendars->append_datum(DatumArray{});
        calendar_ids->append_datum("CalendarID");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "Calendar ID column should not be set when calendar specification is not set.");
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_non_weekday_calendar_not_supported) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    time_units->append_datum("HOURS");
    calendars->append_datum(DatumArray{R"({"intersect_calendar": { } })"});
    calendar_ids->append_datum(kNullDatum);
    const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                  calendar_ids});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "Non weekday calendar is not supported.");
}

} // namespace starrocks
