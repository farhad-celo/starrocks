#include "exprs/celonis/time_functions.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "types/timestamp_value.h"
#include "util.h"
#include <gtest/gtest.h>
#include "testutil/function_utils.h"
#include "exprs/anyval_util.h"
#include "google/protobuf/text_format.h"

namespace starrocks {

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

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_multi_weekday_calendar_without_id) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"multi_weekday_calendar": {)",
                R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_multi_weekday_calendar_with_id) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"multi_weekday_calendar": {)",
                R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                R"(} })"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{
                R"({"multi_weekday_calendar": {)",
                R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 0, "end": 61200000}}, "calendar_id": "id2"},)",
                R"(} })"});
        calendar_ids->append_datum("id2");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(17L, result->get(0).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_intersect_calendar) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(
                DatumArray{R"({"intersect_calendar": {"calendar1": {"multi_weekday_calendar": {)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 0, "end": 61200000}}, "calendar_id": "id2"},)",
                           R"( }}, )",
                           R"("calendar2": {"multi_weekday_calendar": {)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                           R"(}})",
                           R"(}})"});
        calendar_ids->append_datum("id2");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(
                DatumArray{R"({"intersect_calendar": {"calendar1": {"multi_weekday_calendar": {)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                           R"( }}, )",
                           R"("calendar2": {"multi_weekday_calendar": {)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 0, "end": 61200000}}, "calendar_id": "id2"},)",
                           R"(}})",
                           R"(}})"});
        calendar_ids->append_datum("id2");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(
                DatumArray{R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                           R"("entries": {"start_date": 10000000, "end_date": 61200000, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": 0, "end_date": 61200000, "calendar_id": "id2"}, )",
                           R"( }}, )",
                           R"("calendar2": {"multi_weekday_calendar": {)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                           R"(}})",
                           R"(}})"});
        calendar_ids->append_datum("id2");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(
                DatumArray{R"({"intersect_calendar": {"calendar1": {"multi_weekday_calendar": {)",
                           R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                           R"( }}, )",
                           R"("calendar2": {"factory_calendar": {)",
                           R"("entries": {"start_date": 10000000, "end_date": 61200000, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": 0, "end_date": 61200000, "calendar_id": "id2"}, )",
                           R"(}})",
                           R"(}})"});
        calendar_ids->append_datum("id2");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
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
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        ::celonis::accelerator::Calendar calendar_proto;
        google::protobuf::TextFormat::ParseFromString(R"(
        multi_weekday_calendar {
          calendars {
            thursday {
              use_day: true
              shift {
                begin: 0
                end: 1000
              }
            }
          }
        }
        )", &calendar_proto);
        std::string encoded_string = celonis::to_base64_encoded_string(calendar_proto);
        calendars->append_datum(DatumArray{encoded_string.c_str()});

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
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        ::celonis::accelerator::Calendar calendar_proto;
        google::protobuf::TextFormat::ParseFromString(R"(
        multi_weekday_calendar {
          calendars {
            thursday {
              use_day: true
              shift {
                begin: 0
                end: 1000
              }
            }
          }
        }
        )", &calendar_proto);
        std::string encoded_string = celonis::to_base64_encoded_string(calendar_proto);
        std::string first_half = encoded_string.substr(0, encoded_string.length() / 2);
        std::string second_half = encoded_string.substr(encoded_string.length() / 2);
        calendars->append_datum(DatumArray{first_half.c_str(), second_half.c_str()});
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

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_factory_calendar_without_id) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // 01/01/1970 [8:00 am, 5:00 pm]
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // 01/01/1970 [8:00 am, 5:00 pm]
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"});
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
        timestamps->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        time_units->append_datum("HOURS");
        // 01/01/1970 [8:00 am, 5:00 pm]
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(2L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        time_units->append_datum("HOURS");
        // no calendar entries specified
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": { }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("MILLISECONDS");
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": {)",
                           R"("entries": {"start_date": 1, "end_date": 11}, )",
                           R"("entries": {"start_date": 7, "end_date": 21}, )",
                           R"("entries": {"start_date": 500, "end_date": 600}, )",
                           R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(120L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("MILLISECONDS");
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": {)",
                           R"("entries": {"start_date": 1000, "end_date": 2000}, )",
                           R"("entries": {"start_date": 1200, "end_date": 1400}, )",
                           R"("entries": {"start_date": 100, "end_date": 200}, )",
                           R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(1100L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        time_units->append_datum("MILLISECONDS");
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": {)",
                           R"("entries": {"start_date": -11, "end_date": -1}, )",
                           R"("entries": {"start_date": -40, "end_date": -20}, )",
                           R"("entries": {"start_date": -300, "end_date": -20}, )",
                           R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-290L, result->get(0).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_factory_calendar_with_id) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // 01/01/1970 [8:00 am, 5:00 pm]
        calendars->append_datum(
                DatumArray{
                        R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "id"} }})"});
        calendar_ids->append_datum("id");
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
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // 01/01/1970 [8:00 am, 5:00 pm]
        calendars->append_datum(
                DatumArray{
                        R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "id"} }})"});
        calendar_ids->append_datum("new_id");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        time_units->append_datum("MILLISECONDS");
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": {)",
                           R"("entries": {"start_date": -11, "end_date": -1, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": -40, "end_date": -20, "calendar_id": "id2"}, )",
                           R"("entries": {"start_date": -300, "end_date": -20, "calendar_id": "id2"}, )",
                           R"( }})"});
        calendar_ids->append_datum("id2");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-280L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": {)",
                           R"("entries": {"start_date": -1000, "end_date": 2000, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": 3000, "end_date": 4000, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": 5000, "end_date": 6000, "calendar_id": "id1"}, )",
                           R"( }})"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-1L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": {)",
                           R"("entries": {"start_date": -1000, "end_date": 2000, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": 3000, "end_date": 4000, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": 5000, "end_date": 6000, "calendar_id": "id1"}, )",
                           R"( }})"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(4L, result->get(0).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_workday_calendar_without_id) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // Set 01/01/1970 as workday
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": )",
                        R"({ "entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0}).c_str(),
                        R"( } }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(24L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // Set 01/02/1970 as workday
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": )",
                        R"({ "entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {1}).c_str(),
                        R"( } }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        time_units->append_datum("HOURS");
        // Set 01/02/1970 as workday
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": )",
                        R"({ "entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {1}).c_str(),
                        R"( } }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(10L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                        "},",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                        "},",
                        R"("entries": { "year": 1971, )",
                        celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                        "},",
                        R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(8L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1969, 12, 15, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1969, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 360, 361, 362, 364}).c_str(),
                        "},",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 1, 2}).c_str(),
                        "},",
                        R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-4L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1968, 12, 15, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                        "},",
                        R"("entries": { "year": 1969, )",
                        celonis::get_is_workdays_str(365, {0, 1, 10, 100, 150, 364}).c_str(),
                        "},",
                        R"("entries": { "year": 1968, )",
                        celonis::get_is_workdays_str(366, {5, 7, 364, 365}).c_str(),
                        "},",
                        R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(-8L, result->get(0).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_workday_calendar_with_id) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"("entries": { "year": 1971, )",
                        celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(7L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"("entries": { "year": 1971, )",
                        celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"( }})"});
        // no calendars matched id3
        calendar_ids->append_datum("id3");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1990, 2, 1, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"("entries": { "year": 1971, )",
                        celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"( }})"});
        calendar_ids->append_datum("id2");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        // 5 days
        EXPECT_EQ(432000L, result->get(0).get_int64());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_weekday_calendar_multi_rows) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1990, 2, 1, 0, 0, 0));
    time_units->append_datum("MINUTES");
    time_units->append_datum("HOURS");
    time_units->append_datum("DAYS");
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
    calendars->append_datum(
            DatumArray{
                    R"({"workday_calendar": {)",
                    R"("entries": { "year": 1970, )",
                    celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                    R"(, calendar_id: "id1"},)",
                    R"("entries": { "year": 1970, )",
                    celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                    R"(, calendar_id: "id2"},)",
                    R"("entries": { "year": 1971, )",
                    celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                    R"(, calendar_id: "id1"},)",
                    R"( }})"});
    calendar_ids->append_datum(kNullDatum);
    calendar_ids->append_datum(kNullDatum);
    calendar_ids->append_datum("id2");
    const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                  calendar_ids}).value();
    ASSERT_EQ(timestamps->size(), result->size());
    EXPECT_EQ(660L, result->get(0).get_int64());
    EXPECT_EQ(9L, result->get(1).get_int64());
    EXPECT_EQ(5L, result->get(2).get_int64());
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

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_calendar_id_is_ignored) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // 01/01/1970 [8:00 am, 5:00 pm]
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"});
        calendar_ids->append_datum("id1");
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
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // Set 01/01/1970 as workday
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": )",
                        R"({ "entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0}).c_str(),
                        R"( } }})"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(24L, result->get(0).get_int64());
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
    EXPECT_EQ(result.status().get_error_msg(),
              "time unit must be one of WORKDAYS/DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
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
    // mixed calendar_id entry and no-calendar_id entry
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(
                DatumArray{R"({"factory_calendar": {)",
                           R"("entries": {"start_date": -11, "end_date": -1, "calendar_id": "id1"}, )",
                           R"("entries": {"start_date": -40, "end_date": -20, "calendar_id": "id2"}, )",
                           R"("entries": {"start_date": -300, "end_date": -20}, )",
                           R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "In FactoryCalendar, ensure that the calendar_id is either set or not set in all entries.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"("entries": { "year": 1971, )",
                        celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                        R"(},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "In WorkdayCalendar, ensure that the calendar_id is either set or not set in all entries.");
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_calendar_id_not_provided) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        // 01/01/1970 [8:00 am, 5:00 pm]
        calendars->append_datum(
                DatumArray{
                        R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "id"} }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar ID column not provided.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"("entries": { "year": 1971, )",
                        celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"( }})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar ID column not provided.");
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
                R"("monday": {"use_day": true, "shift": {"begin": 0, "end": 1000} }, "calendar_id": "id" )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "calendar_id should not be set in WeekdayCalendar.");
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

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_malformed_multi_weekday_calendar) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray{
                R"({"multi_weekday_calendar": {)",
                R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                R"("calendars": {"friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "In MultiWeekdayCalendar, ensure that the calendar_id is either set or not set in all calendars.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray{
                R"({"multi_weekday_calendar": {)",
                R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                R"("calendars": {"friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "In MultiWeekdayCalendar, two calendars must not share the same calendar_id.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray{
                R"({"multi_weekday_calendar": {)",
                R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                R"("calendars": {"friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "In MultiWeekdayCalendar, two calendars must not share the same calendar_id.");
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_malformed_intersect_calendar) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");
        calendars->append_datum(DatumArray{R"({"intersect_calendar": {}})"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Intersect calendar must set both calendar1 and calendar2.");
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_invalid_factory_calendar_entries_are_ignored) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    time_units->append_datum("MILLISECONDS");

    calendars->append_datum(
            DatumArray{R"({"factory_calendar": {)",
                    // missing start_date
                       R"("entries": {"end_date": 2000, "calendar_id": "id1"}, )",
                    // missing end_date
                       R"("entries": {"start_date": 3000,"calendar_id": "id1"}, )",
                    // start_date >end_date
                       R"("entries": {"start_date": 8000, "end_date": 6000, "calendar_id": "id1"}, )",
                       R"("entries": {"start_date": 1000, "end_date": 6000, "calendar_id": "id1"}, )",
                       R"( }})"});
    calendar_ids->append_datum("id1");
    const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                  calendar_ids}).value();
    ASSERT_EQ(timestamps->size(), result->size());
    EXPECT_EQ(5000L, result->get(0).get_int64());
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_calendar_malformed_workday_calendar) {
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");

        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": )",
                        R"({ "entries": {"year": 1989,)",
                        celonis::get_is_workdays_str(366, {0, 2, 3}).c_str(),
                        R"( } }})"});
        calendar_ids->append_datum("id");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "1989 should have 365 days, however the workday calendar contains 366 is_workday.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("HOURS");

        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": )",
                        R"({ "entries": {)",
                        celonis::get_is_workdays_str(365, {0, 2, 3}).c_str(),
                        R"( } }})"});
        calendar_ids->append_datum("id");
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(nullptr, {timestamps, time_units, calendars,
                                                                                      calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "year is not set in a workday calendar entry.");
    }
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
    const auto result = CelonisTimeFunctions::date_between(nullptr,
                                                           {timestamps, begin_timestamps, end_timestamps}).value();
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
        const auto result = CelonisTimeFunctions::date_between(nullptr,
                                                               {timestamps, begin_timestamps, end_timestamps}).value();
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
        const auto result = CelonisTimeFunctions::date_between(nullptr,
                                                               {timestamps, begin_timestamps, end_timestamps}).value();
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
        const auto result = CelonisTimeFunctions::date_between(nullptr,
                                                               {timestamps, begin_timestamps, end_timestamps}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTimeFunctionsTest, date_between_empty_input) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto begin_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto end_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    const auto result = CelonisTimeFunctions::date_between(nullptr,
                                                           {timestamps, begin_timestamps, end_timestamps}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_calendar_prepare) {
    // Single row
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto timeunits = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        timeunits->append_datum("HOURS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 10, 15}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {11}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        auto utils = std::make_shared<FunctionUtils>();
        auto const_calendars = ConstColumn::create(calendars, from_timestamps->size());
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, nullptr, const_calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_prepare(utils->get_fn_ctx(),
                                                                             FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
        // execute
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(utils->get_fn_ctx(),
                                                                             {from_timestamps, to_timestamps, timeunits,
                                                                              calendars, calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(9.0, result->get(0).get_double());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_close(utils->get_fn_ctx(),
                                                                           FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    // Multiple rows
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto timeunits = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        timeunits->append_datum("HOURS");
        timeunits->append_datum("HOURS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 10, 15}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {11}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        calendar_ids->append_datum("id1");
        auto utils = std::make_shared<FunctionUtils>();
        auto const_calendars = ConstColumn::create(calendars, from_timestamps->size());
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, nullptr, const_calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_prepare(utils->get_fn_ctx(),
                                                                             FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
        // execute
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(utils->get_fn_ctx(),
                                                                             {from_timestamps, to_timestamps, timeunits,
                                                                              calendars, calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(9.0, result->get(0).get_double());
        EXPECT_EQ(14.0, result->get(1).get_double());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_close(utils->get_fn_ctx(),
                                                                           FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    // NULL calendar
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto timeunits = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        timeunits->append_datum("HOURS");
        calendars->append_datum(kNullDatum);
        calendar_ids->append_datum("id1");
        auto utils = std::make_shared<FunctionUtils>();
        auto const_calendars = ConstColumn::create(calendars, from_timestamps->size());
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, nullptr, const_calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_prepare(utils->get_fn_ctx(),
                                                                             FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
        // execute
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(utils->get_fn_ctx(),
                                                                             {from_timestamps, to_timestamps, timeunits,
                                                                              calendars, calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_close(utils->get_fn_ctx(),
                                                                           FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    // Empty calendar
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto timeunits = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        timeunits->append_datum("HOURS");
        calendars->append_datum(DatumArray{});
        calendar_ids->append_datum(kNullDatum);
        auto utils = std::make_shared<FunctionUtils>();
        auto const_calendars = ConstColumn::create(calendars, from_timestamps->size());
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, nullptr, const_calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_prepare(utils->get_fn_ctx(),
                                                                             FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
        // execute
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(utils->get_fn_ctx(),
                                                                             {from_timestamps, to_timestamps, timeunits,
                                                                              calendars, calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(33.0, result->get(0).get_double());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::timeunits_between_calendar_close(utils->get_fn_ctx(),
                                                                           FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    // Malformed calendar
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto timeunits = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        timeunits->append_datum("HOURS");
        calendars->append_datum(DatumArray{"Unknown"});
        calendar_ids->append_datum(kNullDatum);
        auto utils = std::make_shared<FunctionUtils>();
        auto const_calendars = ConstColumn::create(calendars, from_timestamps->size());
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, nullptr, const_calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        const auto result = CelonisTimeFunctions::timeunits_between_calendar_prepare(utils->get_fn_ctx(),
                                                                                     FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        ASSERT_TRUE(result.is_invalid_argument());
        EXPECT_EQ(result.get_error_msg(), "[prepare] Calendar specification column is malformed.");
    }
    // NULL in Calendar json string array
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto timeunits = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        timeunits->append_datum("HOURS");
        calendars->append_datum(DatumArray{kNullDatum});
        calendar_ids->append_datum(kNullDatum);
        auto utils = std::make_shared<FunctionUtils>();
        auto const_calendars = ConstColumn::create(calendars, from_timestamps->size());
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, nullptr, const_calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        const auto result = CelonisTimeFunctions::timeunits_between_calendar_prepare(utils->get_fn_ctx(),
                                                                                     FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        ASSERT_TRUE(result.is_invalid_argument());
        EXPECT_EQ(result.get_error_msg(), "[prepare] Calendar array can not contain null values.");
    }
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_calendar_without_calendar) {
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 0, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 12, 0, 0));
        time_units->append_datum("WORKDAYS");
        calendars->append_datum(DatumArray());
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(1.0, result->get(0).get_double());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 0, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 12, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(DatumArray());
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(1.5, result->get(0).get_double());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 5, 0, 0));
        from_timestamps->append_datum(TimestampValue::create(2000, 1, 1, 23, 29, 59, 999000));
        to_timestamps->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            time_units->append_datum("HOURS");
            calendars->append_datum(DatumArray());
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(4.0, result->get(0).get_double());
        EXPECT_EQ(-23.5, result->get(1).get_double());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 1, 10, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 2, 20, 0));
        from_timestamps->append_datum(TimestampValue::create(2000, 1, 1, 1, 0, 29, 999000));
        to_timestamps->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            time_units->append_datum("MINUTES");
            calendars->append_datum(DatumArray());
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(70.0, result->get(0).get_double());
        EXPECT_EQ(-60.5, result->get(1).get_double());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2000, 1, 1, 0, 0, 59, 999000));
        to_timestamps->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
        from_timestamps->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 10));
        to_timestamps->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 11, 500000));
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            time_units->append_datum("SECONDS");
            calendars->append_datum(DatumArray());
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(-60.0, result->get(0).get_double());
        EXPECT_EQ(1.5, result->get(1).get_double());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2000, 1, 1, 0, 0, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
        from_timestamps->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 10));
        to_timestamps->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 11, 0));
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            time_units->append_datum("MILLISECONDS");
            calendars->append_datum(DatumArray());
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(-1.0, result->get(0).get_double());
        EXPECT_EQ(1000.0, result->get(1).get_double());
    }
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_calendar_weekday_calendar) {
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 2, 0, 0));
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            time_units->append_datum("DAYS");
            calendars->append_datum(DatumArray{
                    R"({"weekday_calendar": {)",
                    // [8:00 am, 5:00 pm]
                    R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("saturday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"(} })"});
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(1.125, result->get(0).get_double());
        EXPECT_EQ(-1.875, result->get(1).get_double());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 2, 0, 0));

        for (auto i = 0; i < from_timestamps->size(); ++i) {
            time_units->append_datum("WORKDAYS");
            calendars->append_datum(DatumArray{
                    R"({"weekday_calendar": {)",
                    // [8:00 am, 5:00 pm]
                    R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"("saturday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                    R"(} })"});
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(3.0, result->get(0).get_double());
        EXPECT_EQ(-5.0, result->get(1).get_double());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 1, 2, 0, 0));
        ::celonis::accelerator::Calendar calendar_proto;
        google::protobuf::TextFormat::ParseFromString(R"(
            multi_weekday_calendar {
              calendars {
                monday {
                  use_day: true
                  shift {
                    begin: 28800000
                    end: 61200000
                  }
                }
                tuesday {
                  use_day: true
                  shift {
                    begin: 28800000
                    end: 61200000
                  }
                }
                thursday {
                  use_day: true
                  shift {
                    begin: 28800000
                    end: 61200000
                  }
                }
                friday {
                  use_day: true
                  shift {
                    begin: 28800000
                    end: 61200000
                  }
                }
                saturday {
                  use_day: true
                  shift {
                    begin: 28800000
                    end: 61200000
                  }
                }
              }
            }
        )", &calendar_proto);
        std::string encoded_string = celonis::to_base64_encoded_string(calendar_proto);
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            time_units->append_datum("WORKDAYS");
            calendars->append_datum(DatumArray{encoded_string.c_str()});
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_EQ(3.0, result->get(0).get_double());
        EXPECT_EQ(-5.0, result->get(1).get_double());
    }
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_calendar_intersect_calendar) {
    auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
    to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
    from_timestamps->append_datum(TimestampValue::create(2018, 1, 9, 0, 0, 0));
    to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
    for (auto i = 0; i < from_timestamps->size(); ++i) {
        time_units->append_datum("WORKDAYS");
        calendars->append_datum(
                DatumArray{R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                           R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000}, )",
                           R"("entries": {"start_date": 1514966400000, "end_date": 1514998800000}, )",
                           R"("entries": {"start_date": 1515052800000, "end_date": 1515085200000}, )",
                           R"("entries": {"start_date": 1515225600000, "end_date": 1515258000000}, )",
                           R"("entries": {"start_date": 1515312000000, "end_date": 1515326400000}, )",
                           R"("entries": {"start_date": 1515398400000, "end_date": 1515430800000}, )",
                           R"("entries": {"start_date": 1515484800000, "end_date": 1515517200000}, )",
                           R"( }}, )",
                           R"("calendar2": {"weekday_calendar": {)",
                           R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                           R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                           R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                           R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                           R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                           R"("sunday": {"use_day": true, "shift": {"begin": 46800000, "end": 54000000} }, )",
                           R"(}})",
                           R"(}})"});
        calendar_ids->append_datum(kNullDatum);
    }
    const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                         {from_timestamps, to_timestamps,
                                                                          time_units, calendars,
                                                                          calendar_ids}).value();
    ASSERT_EQ(from_timestamps->size(), result->size());
    EXPECT_EQ(2.0, result->get(0).get_double());
    EXPECT_EQ(-2.0, result->get(1).get_double());
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_outside_of_scope) {
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        time_units->append_datum("WORKDAYS");
        time_units->append_datum("DAYS");
        time_units->append_datum("HOURS");
        time_units->append_datum("MINUTES");
        time_units->append_datum("SECONDS");
        time_units->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_units->size(); ++i) {
            from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendars->append_datum(DatumArray{
                    R"({"factory_calendar": {)",
                    R"("entries": {"start_date": 0, "end_date": 1000 })",
                    R"(} })"});
            calendar_ids->append_datum(kNullDatum);
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            EXPECT_TRUE(result->get(i).is_null());
        }
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        time_units->append_datum("WORKDAYS");
        time_units->append_datum("DAYS");
        time_units->append_datum("HOURS");
        time_units->append_datum("MINUTES");
        time_units->append_datum("SECONDS");
        time_units->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_units->size(); ++i) {
            from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendars->append_datum(DatumArray{
                    R"({"factory_calendar": {)",
                    R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "id1"}, )",
                    R"("entries": {"start_date": 1514768400000, "end_date": 1515546000000, "calendar_id": "id2"})",
                    R"(} })"});
            calendar_ids->append_datum("id1");
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            EXPECT_TRUE(result->get(i).is_null());
        }
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        time_units->append_datum("WORKDAYS");
        time_units->append_datum("DAYS");
        time_units->append_datum("HOURS");
        time_units->append_datum("MINUTES");
        time_units->append_datum("SECONDS");
        time_units->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_units->size(); ++i) {
            from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendars->append_datum(DatumArray{
                    R"({"factory_calendar": {)",
                    R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "id1"}, )",
                    R"("entries": {"start_date": 1514768400000, "end_date": 1515546000000, "calendar_id": "id2"})",
                    R"(} })"});
            calendar_ids->append_datum("id2");
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            EXPECT_FALSE(result->get(i).is_null());
        }
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        time_units->append_datum("WORKDAYS");
        time_units->append_datum("DAYS");
        time_units->append_datum("HOURS");
        time_units->append_datum("MINUTES");
        time_units->append_datum("SECONDS");
        time_units->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_units->size(); ++i) {
            from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendars->append_datum(DatumArray{
                    R"({"factory_calendar": {)",
                    R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "id1"}, )",
                    R"("entries": {"start_date": 1514768400000, "end_date": 1515546000000, "calendar_id": "id2"})",
                    R"(} })"});
            calendar_ids->append_datum("id");
        }
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        for (auto i = 0; i < from_timestamps->size(); ++i) {
            EXPECT_EQ(0.0, result->get(i).get_double());
        }
    }
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_calendar_null_input) {
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(kNullDatum);
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm]
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        to_timestamps->append_datum(kNullDatum);
        time_units->append_datum("DAYS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm]
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_units->append_datum(kNullDatum);
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm]
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(kNullDatum);
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids}).value();
        ASSERT_EQ(from_timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_calendar_invalid_input) {
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                // [8:00 am, 5:00 pm]
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar array should not have null elements.");
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        from_timestamps->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_units->append_datum("UNKNOWN");
        calendars->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm]
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"(} })"});
        calendar_ids->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "time unit must be one of DAYS/WORKDAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
    }
}

TEST_F(CelonisTimeFunctionsTest, timeunits_between_calendar_year_gaps_in_workday_calendar) {
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 6, 1, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 10, 15}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1972, )",
                        celonis::get_is_workdays_str(366, {11}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Year gaps are found in the workday calendar configuration.");
    }
    {
        auto from_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        auto to_timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        from_timestamps->append_datum(TimestampValue::create(1970, 1, 2, 1, 0, 0));
        to_timestamps->append_datum(TimestampValue::create(1970, 1, 6, 1, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 10, 15}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1972, )",
                        celonis::get_is_workdays_str(366, {11}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        const auto result = CelonisTimeFunctions::timeunits_between_calendar(nullptr,
                                                                             {from_timestamps, to_timestamps,
                                                                              time_units, calendars,
                                                                              calendar_ids});
        ASSERT_TRUE(result.status().ok());
    }
}

TEST_F(CelonisTimeFunctionsTest, remap_timestamps_prepare) {
    // Single row
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 13, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 10, 15}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {11}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        auto utils = std::make_shared<FunctionUtils>();
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_prepare(utils->get_fn_ctx(),
                                                                            FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
        // execute
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(utils->get_fn_ctx(),
                                                                            {timestamps, time_units, calendars,
                                                                             calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(2L, result->get(0).get_int64());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_close(utils->get_fn_ctx(),
                                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    // multiple rows
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 13, 0, 0, 0));
        timestamps->append_datum(TimestampValue::create(1970, 1, 13, 0, 0, 0));
        time_units->append_datum("DAYS");
        time_units->append_datum("DAYS");
        calendars->append_datum(
                DatumArray{
                        R"({"workday_calendar": {)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0, 10, 15}).c_str(),
                        R"(, calendar_id: "id1"},)",
                        R"("entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {11}).c_str(),
                        R"(, calendar_id: "id2"},)",
                        R"( }})"});
        calendar_ids->append_datum("id1");
        calendar_ids->append_datum("id2");
        auto utils = std::make_shared<FunctionUtils>();
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_prepare(utils->get_fn_ctx(),
                                                                            FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
        // execute
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(utils->get_fn_ctx(),
                                                                            {timestamps, time_units, calendars,
                                                                             calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(2L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_close(utils->get_fn_ctx(),
                                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    // empty calendar array
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(DatumArray{});
        calendar_ids->append_datum(kNullDatum);
        auto utils = std::make_shared<FunctionUtils>();
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_prepare(utils->get_fn_ctx(),
                                                                            FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

        // execute
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(utils->get_fn_ctx(),
                                                                            {timestamps, time_units, calendars,
                                                                             calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_close(utils->get_fn_ctx(),
                                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    // NULL calendar
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        timestamps->append_datum(TimestampValue::create(1970, 1, 13, 0, 0, 0));
        time_units->append_datum("DAYS");
        calendars->append_datum(kNullDatum);
        calendar_ids->append_datum(kNullDatum);
        auto utils = std::make_shared<FunctionUtils>();
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_prepare(utils->get_fn_ctx(),
                                                                            FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

        // execute
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar(utils->get_fn_ctx(),
                                                                            {timestamps, time_units, calendars,
                                                                             calendar_ids}).value();
        ASSERT_EQ(timestamps->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        // close
        ASSERT_TRUE(CelonisTimeFunctions::remap_timestamps_calendar_close(utils->get_fn_ctx(),
                                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 12, 1, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray{"Unknown"});
        calendar_ids->append_datum("id1");
        auto utils = std::make_shared<FunctionUtils>();
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar_prepare(utils->get_fn_ctx(),
                                                                                    FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        ASSERT_TRUE(result.is_invalid_argument());
        EXPECT_EQ(result.get_error_msg(), "[prepare] Calendar specification column is malformed.");
    }
    {
        auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto time_units = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto calendars = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendar_ids = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        timestamps->append_datum(TimestampValue::create(1970, 1, 12, 1, 0, 0));
        time_units->append_datum("SECONDS");
        calendars->append_datum(DatumArray{kNullDatum});
        calendar_ids->append_datum("id1");
        auto utils = std::make_shared<FunctionUtils>();
        utils->get_fn_ctx()->set_constant_columns({nullptr, nullptr, calendars, nullptr});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_DATETIME});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_ARRAY});
        utils->get_fn_ctx()->_arg_types.emplace_back(FunctionContext::TypeDesc{TYPE_VARCHAR});

        // prepare
        const auto result = CelonisTimeFunctions::remap_timestamps_calendar_prepare(utils->get_fn_ctx(),
                                                                                    FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        ASSERT_TRUE(result.is_invalid_argument());
        EXPECT_EQ(result.get_error_msg(), "[prepare] Calendar array can not contain null values.");
    }
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_const_null_column) {
    // calendars1 is const null column
    {
        auto calendars1 = ColumnHelper::create_const_null_column(1);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
    // calendars2 is const null column
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        auto calendars2 = ColumnHelper::create_const_null_column(1);
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
    // both calendars are const null columns
    {
        auto calendars1 = ColumnHelper::create_const_null_column(2);
        auto calendars2 = ColumnHelper::create_const_null_column(2);
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_const_input) {
    // calendar1 is const
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars1 = ConstColumn::create(calendars1, calendars1->size());
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
    }
    // calendar2 is const
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2 = ConstColumn::create(calendars2, calendars2->size());
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
    }
    // both calendar1 and calendar2 are const
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars1 = ConstColumn::create(calendars1, calendars1->size());
        calendars2 = ConstColumn::create(calendars2, calendars2->size());
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
    }
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_empty_input) {

    auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
    ASSERT_EQ(calendars1->size(), result->size());
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_multiple_rows) {
    auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    calendars1->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendars1->append_datum(kNullDatum);
    calendars1->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 2000} })",
            R"(} })"});
    calendars2->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendars2 = ConstColumn::create(calendars2, calendars1->size());
    const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
    ASSERT_EQ(calendars1->size(), result->size());
    auto json_string1 = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
    ASSERT_TRUE(json_string1.has_value());
    EXPECT_EQ(json_string1.value(),
              R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
    EXPECT_TRUE(result->get(1).is_null());
    auto json_string2 = celonis::to_calendar_json_string(result->get(2).get_array()[0].get_slice().to_string());
    ASSERT_TRUE(json_string2.has_value());
    EXPECT_EQ(json_string2.value(),
              R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":2000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar) {
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
    }
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"factory_calendar": {)",
                R"("entries": {"start_date": -100, "end_date": 100 })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"factoryCalendar":{"entries":[{"startDate":"-100","endDate":"100"}]}}}})");
    }
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_long_calendar) {
    auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    const size_t n_entries = 20000;
    DatumArray array;
    array.emplace_back(R"({"factory_calendar": {)");
    for (size_t i = 0; i < n_entries; ++i) {
        array.emplace_back(R"("entries": {"start_date": -86400000, "end_date": 3600000, "calendar_id": "id1" }, )");
    }
    array.emplace_back(R"(} })");
    calendars1->append_datum(array);
    calendars2->append_datum(array);
    const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
    ASSERT_EQ(calendars1->size(), result->size());
    EXPECT_EQ(2L, result->get(0).get_array().size());
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_empty_calendar_array) {
    // calendar1 array is empty
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"intersectCalendar":{"calendar1":{},"calendar2":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
    }
    // calendar2 array is empty
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{}}})");
    }
    // both calendar1 and calendar2 arrays are empty
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{});
        calendars2->append_datum(DatumArray{});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        auto json_string = celonis::to_calendar_json_string(result->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"intersectCalendar":{"calendar1":{},"calendar2":{}}})");
    }
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_null_input) {
    // calendar1 is NULL
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(kNullDatum);
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // calendar2 is NULL
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // both calendar1 and calendar2 is NULL
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        calendars1->append_datum(kNullDatum);
        calendars2->append_datum(kNullDatum);
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_null_value_in_calendar_array) {
    // null value in calendar1 array
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // null value in calendar2 array
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // null value in both calendar1 and calendar2
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTimeFunctionsTest, make_intersect_calendar_malformed_calendar) {
    // calendar1 is malformed
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{"Unknown"});
        calendars2->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // calendar2 is malformed
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        calendars2->append_datum(DatumArray{"Unknown"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // both calendar1 and calendar2 are malformed
    {
        auto calendars1 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto calendars2 = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        calendars1->append_datum(DatumArray{"Unknown"});
        calendars2->append_datum(DatumArray{"Unknown"});
        const auto result = CelonisTimeFunctions::make_intersect_calendar(nullptr, {calendars1, calendars2}).value();
        ASSERT_EQ(calendars1->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

} // namespace starrocks
