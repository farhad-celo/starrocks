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
    col->append_datum(-11676096000000L);  // 1600-01-01 00:00:00
    col->append_datum(61123L);    // 1970-01-01 00:01:01.123 UTC
    col->append_datum(172800000L);  // 1970-01-03 00:00:00
    col->append_datum(-11676182400000L);  // 1599-12-31 00:00:00

    const auto result = CelonisTimeFunctions::timestamp_millis(nullptr, {col}).value();
    ASSERT_EQ(result->size(), col->size());
    EXPECT_EQ(result->get(0).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0));
    EXPECT_EQ(result->get(1).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 0, 0, 123000));
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ(result->get(3).get_timestamp(), TimestampValue::create(1600, 1, 1, 0, 0, 0, 0));
    EXPECT_EQ(result->get(4).get_timestamp(), TimestampValue::create(1970, 1, 1, 0, 1, 1, 123000));
    EXPECT_EQ(result->get(5).get_timestamp(), TimestampValue::create(1970, 1, 3, 0, 0, 0));
    EXPECT_EQ(result->get(6).get_timestamp(), TimestampValue::create(1599, 12, 31, 0, 0, 0, 0));
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
