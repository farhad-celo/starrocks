#include "exprs/celonis/time_functions.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

namespace starrocks {

class CelonisRemapTimestampsCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        timestamp_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        time_unit_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        calendar_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
        calendar_id_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisTimeFunctions::remap_timestamps_calendar_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::remap_timestamps_calendar_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisTimeFunctions::remap_timestamps_calendar_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::remap_timestamps_calendar_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisTimeFunctions::remap_timestamps_calendar(ctx_.get(),
                                                                 {timestamp_column_, time_unit_column_,
                                                                  calendar_column_, calendar_id_column_});
        return result;
    }

    StatusOr<ColumnPtr>
    RunConstantCalendar(const std::vector<std::string>& calendar_strs) {
        DatumArray calendar_array;
        for (const auto& calendar_str: calendar_strs) {
            calendar_array.emplace_back(calendar_str.c_str());
        }
        calendar_column_->append_datum(calendar_array);
        calendar_column_ = ConstColumn::create(calendar_column_, timestamp_column_->size());
        ctx_->set_constant_columns({nullptr, nullptr, calendar_column_, nullptr});
        return Run();
    }

    StatusOr<ColumnPtr>
    RunConstantCalendar() {
        ctx_->set_constant_columns(
                {nullptr, nullptr, ConstColumn::create(calendar_column_, timestamp_column_->size()), nullptr});
        return Run();
    }

    StatusOr<ColumnPtr>
    RunConstantCalendarAndTimeUnit(const std::vector<std::string>& calendar_strs, const std::string& time_unit) {
        DatumArray calendar_array;
        for (const auto& calendar_str: calendar_strs) {
            calendar_array.emplace_back(calendar_str.c_str());
        }
        calendar_column_->append_datum(calendar_array);
        calendar_column_ = ConstColumn::create(calendar_column_, timestamp_column_->size());
        time_unit_column_->append_datum(Slice(time_unit));
        time_unit_column_ = ConstColumn::create(time_unit_column_, timestamp_column_->size());
        ctx_->set_constant_columns({nullptr, time_unit_column_, calendar_column_, nullptr});
        return Run();
    }

    StatusOr<ColumnPtr>
    RunConstantCalendarAndTimeUnit() {
        ctx_->set_constant_columns(
                {nullptr, ConstColumn::create(time_unit_column_, timestamp_column_->size()),
                 ConstColumn::create(calendar_column_, timestamp_column_->size()), nullptr});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr timestamp_column_;
    ColumnPtr time_unit_column_;
    ColumnPtr calendar_column_;
    ColumnPtr calendar_id_column_;
};

TEST_F(CelonisRemapTimestampsCalendarTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisRemapTimestampsCalendarTest, without_calendar) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 10));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 2, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1971, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1972, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1975, 1, 1, 0, 0, 0));

    time_unit_column_->append_datum("MILLISECONDS");
    time_unit_column_->append_datum("MILLISECONDS");
    time_unit_column_->append_datum("MILLISECONDS");
    time_unit_column_->append_datum("SECONDS");
    time_unit_column_->append_datum("SECONDS");
    time_unit_column_->append_datum("SECONDS");
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("DAYS");

    for (auto i = 0; i < timestamp_column_->size(); ++i) {
        calendar_id_column_->append_datum(kNullDatum);
    }

    const auto result = RunConstantCalendar({}).value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(3600000L, result->get(1).get_int64());
    EXPECT_EQ(10000L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(86400L, result->get(4).get_int64());
    EXPECT_EQ(-86400L, result->get(5).get_int64());
    EXPECT_EQ(0L, result->get(6).get_int64());
    EXPECT_EQ(44640L, result->get(7).get_int64());
    EXPECT_EQ(8760L, result->get(8).get_int64());
    EXPECT_EQ(17520L, result->get(9).get_int64());
    EXPECT_EQ(1826L, result->get(10).get_int64());
}

TEST_F(CelonisRemapTimestampsCalendarTest, empty_calendar) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendarAndTimeUnit({}, "DAYS").value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
}

TEST_F(CelonisRemapTimestampsCalendarTest, null_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("DAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("DAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(kNullDatum);
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, multi_weekday_calendar_without_id) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendarAndTimeUnit(
            {R"({"multi_weekday_calendar": {)",
             R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
             R"(} })"
            }, "HOURS").value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(9L, result->get(0).get_int64());
}

TEST_F(CelonisRemapTimestampsCalendarTest, multi_weekday_calendar_with_id) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id1");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                 R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 0, "end": 61200000}}, "calendar_id": "id2"},)",
                 R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(17L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id3");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                 R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, intersect_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"intersect_calendar": {"calendar1": {"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 0, "end": 61200000}}, "calendar_id": "id2"},)",
                 R"( }}, )",
                 R"("calendar2": {"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                 R"(}})",
                 R"(}})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"intersect_calendar": {"calendar1": {"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                 R"( }}, )",
                 R"("calendar2": {"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 0, "end": 61200000}}, "calendar_id": "id2"},)",
                 R"(}})",
                 R"(}})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                 R"("entries": {"start_date": 10000000, "end_date": 61200000, "calendar_id": "id1"}, )",
                 R"("entries": {"start_date": 0, "end_date": 61200000, "calendar_id": "id2"}, )",
                 R"( }}, )",
                 R"("calendar2": {"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                 R"(}})",
                 R"(}})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"intersect_calendar": {"calendar1": {"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                 R"( }}, )",
                 R"("calendar2": {"factory_calendar": {)",
                 R"("entries": {"start_date": 10000000, "end_date": 61200000, "calendar_id": "id1"}, )",
                 R"("entries": {"start_date": 0, "end_date": 61200000, "calendar_id": "id2"}, )",
                 R"(}})",
                 R"(}})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, weekday_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
                                                        R"({"weekday_calendar": {)",
                                                        R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                                                        R"(} })"}, "SECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
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
        const auto result = RunConstantCalendarAndTimeUnit({encoded_string.c_str()}, "SECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
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
        const auto result = RunConstantCalendarAndTimeUnit({first_half.c_str(), second_half.c_str()}, "SECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
                                                        R"({"weekday_calendar": {)",
                                                        // [8:00 am, 5:00 pm] = 9 hours
                                                        R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        // use_day = false
                                                        R"("friday": {"use_day": false, "shift": {"begin": 28800000, "end": 61200000} })",
                                                        R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
                                                        R"({"weekday_calendar": {)",
                                                        // [8:00 am, 5:00 pm] = 9 hours
                                                        R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} })",
                                                        R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(11L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
                                                        R"({"weekday_calendar": {)",
                                                        // [8:00 am, 5:00 pm] = 9 hours
                                                        R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} })",
                                                        R"(} })"}, "MINUTES").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(660L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 8, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
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
                                                        R"(} })"}, "MINUTES").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(2160L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 8, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
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
                                                        R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(13L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 13, 10, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
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
                                                        R"(} })"}, "MILLISECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(201600000L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
                                                        R"({"weekday_calendar": {)",
                                                        // [12:00 am, 8:00 am]
                                                        R"("wednesday": {"use_day": true, "shift": {"begin": 0, "end": 28800000} })",
                                                        R"(} })"}, "SECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-28800L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
                                                        R"({"weekday_calendar": {)",
                                                        // [12:00 am, 8:00 am]
                                                        R"("wednesday": {"use_day": true, "shift": {"begin": 0, "end": 28800000} })",
                                                        R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-8L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 1, 1, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({
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
                                                        R"(} })"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-1881L, result->get(0).get_int64());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, factory_calendar_without_id) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        // 01/01/1970 [8:00 am, 5:00 pm]
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        // 01/01/1970 [8:00 am, 5:00 pm]
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        // 01/01/1970 [8:00 am, 5:00 pm]
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(2L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"factory_calendar": {}})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 1, "end_date": 11}, )",
                                                 R"("entries": {"start_date": 7, "end_date": 21}, )",
                                                 R"("entries": {"start_date": 500, "end_date": 600}, )",
                                                 R"( }})"}, "MILLISECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(120L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 1000, "end_date": 2000}, )",
                                                 R"("entries": {"start_date": 1200, "end_date": 1400}, )",
                                                 R"("entries": {"start_date": 100, "end_date": 200}, )",
                                                 R"( }})"}, "MILLISECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(1100L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": -11, "end_date": -1}, )",
                                                 R"("entries": {"start_date": -40, "end_date": -20}, )",
                                                 R"("entries": {"start_date": -300, "end_date": -20}, )",
                                                 R"( }})"}, "MILLISECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-290L, result->get(0).get_int64());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, factory_calendar_with_id) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "id"} }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("new_id");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "id"} }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": -11, "end_date": -1, "calendar_id": "id1"}, )",
                 R"("entries": {"start_date": -40, "end_date": -20, "calendar_id": "id2"}, )",
                 R"("entries": {"start_date": -300, "end_date": -20, "calendar_id": "id2"}, )",
                 R"( }})"}, "MILLISECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-280L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        calendar_id_column_->append_datum("id1");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": -1000, "end_date": 2000, "calendar_id": "id1"}, )",
                 R"("entries": {"start_date": 3000, "end_date": 4000, "calendar_id": "id1"}, )",
                 R"("entries": {"start_date": 5000, "end_date": 6000, "calendar_id": "id1"}, )",
                 R"( }})"}, "SECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-1L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id1");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": -1000, "end_date": 2000, "calendar_id": "id1"}, )",
                 R"("entries": {"start_date": 3000, "end_date": 4000, "calendar_id": "id1"}, )",
                 R"("entries": {"start_date": 5000, "end_date": 6000, "calendar_id": "id1"}, )",
                 R"( }})"}, "SECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(4L, result->get(0).get_int64());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, workday_calendar_without_id) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": )",
                                                 R"({ "entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0}).c_str(),
                                                 R"( } }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(24L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": )",
                                                 R"({ "entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {1}).c_str(),
                                                 R"( } }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": )",
                                                 R"({ "entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {1}).c_str(),
                                                 R"( } }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(10L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": {)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                                                 "},",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                                                 "},",
                                                 R"("entries": { "year": 1971, )",
                                                 celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                                                 "},",
                                                 R"( }})"}, "DAYS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(8L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 15, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": {)",
                                                 R"("entries": { "year": 1969, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 360, 361, 362,
                                                                                    364}).c_str(),
                                                 "},",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 1, 2}).c_str(),
                                                 "},",
                                                 R"( }})"}, "DAYS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-4L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1968, 12, 15, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": {)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                                                 "},",
                                                 R"("entries": { "year": 1969, )",
                                                 celonis::get_is_workdays_str(365, {0, 1, 10, 100, 150, 364}).c_str(),
                                                 "},",
                                                 R"("entries": { "year": 1968, )",
                                                 celonis::get_is_workdays_str(366, {5, 7, 364, 365}).c_str(),
                                                 "},",
                                                 R"( }})"}, "DAYS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(-8L, result->get(0).get_int64());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, workday_calendar_with_id) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        calendar_id_column_->append_datum("id1");
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": {)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_workday_mask_str(365, {0, 4, 10, 100, 150}).c_str(),
                                                 R"(, calendar_id: "id1"},)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_workday_mask_str(365, {0, 4, 10, 100, 364}).c_str(),
                                                 R"(, calendar_id: "id2"},)",
                                                 R"("entries": { "year": 1971, )",
                                                 celonis::get_workday_mask_str(365, {5, 7}).c_str(),
                                                 R"(, calendar_id: "id1"},)",
                                                 R"( }})"}, "DAYS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(7L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1971, 2, 1, 0, 0, 0));
        calendar_id_column_->append_datum("id3");
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": {)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                                                 R"(, calendar_id: "id1"},)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                                                 R"(, calendar_id: "id2"},)",
                                                 R"("entries": { "year": 1971, )",
                                                 celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                                                 R"(, calendar_id: "id1"},)",
                                                 R"( }})"}, "DAYS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1990, 2, 1, 0, 0, 0));
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": {)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                                                 R"(, calendar_id: "id1"},)",
                                                 R"("entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                                                 R"(, calendar_id: "id2"},)",
                                                 R"("entries": { "year": 1971, )",
                                                 celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                                                 R"(, calendar_id: "id1"},)",
                                                 R"( }})"}, "SECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(432000L, result->get(0).get_int64());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, malformed_workday_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id");
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": )",
                                                 R"({ "entries": {"year": 1989,)",
                                                 celonis::get_is_workdays_str(366, {0, 2, 3}).c_str(),
                                                 R"( } }})"}, "HOURS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "1989 should have 365 days, however the workday calendar contains 366 is_workday.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum("id");
        calendar_column_->append_datum(DatumArray{R"({"workday_calendar": )",
                                                  R"({ "entries": {"year": 1989,)",
                                                  celonis::get_is_workdays_str(366, {0, 2, 3}).c_str(),
                                                  R"( } }})"});
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "1989 should have 365 days, however the workday calendar contains 366 is_workday.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id");
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": )",
                                                 R"({ "entries": {)",
                                                 celonis::get_is_workdays_str(365, {0, 2, 3}).c_str(),
                                                 R"( } }})"}, "HOURS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "year is not set in a workday calendar entry.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum("id");

        calendar_column_->append_datum(DatumArray{R"({"workday_calendar": )",
                                                  R"({ "entries": {)",
                                                  celonis::get_is_workdays_str(365, {0, 2, 3}).c_str(),
                                                  R"( } }})"});
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "year is not set in a workday calendar entry.");
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, malformed_intersect_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"intersect_calendar": {}})"}, "HOURS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Intersect calendar must set both calendar1 and calendar2.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(DatumArray{R"({"intersect_calendar": {}})"});
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Intersect calendar must set both calendar1 and calendar2.");
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, calendar_id_provided_when_not_need) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    time_unit_column_->append_datum("MILLISECONDS");
    calendar_column_->append_datum(DatumArray{});
    calendar_id_column_->append_datum("CalendarID");
    const auto result = Run();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(),
              "Calendar ID column should not be set when calendar specification is not set.");
}

TEST_F(CelonisRemapTimestampsCalendarTest, malformed_multi_weekday_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"multi_weekday_calendar": {)",
                                                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                                                 R"("calendars": {"friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                                                 R"(} })"}, "HOURS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "In MultiWeekdayCalendar, ensure that the calendar_id is either set or not set in all calendars.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"multi_weekday_calendar": {)",
                                                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                                                 R"("calendars": {"friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                                                 R"(} })"}, "HOURS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "In MultiWeekdayCalendar, two calendars must not share the same calendar_id.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"multi_weekday_calendar": {)",
                                                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                                                 R"("calendars": {"friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                                                 R"(} })"}, "HOURS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "In MultiWeekdayCalendar, two calendars must not share the same calendar_id.");
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, non_const_calendar) {
    const bool treat_calendar_column_as_constant_in_calendar_functions = config::treat_calendar_column_as_constant_in_calendar_functions;
    config::treat_calendar_column_as_constant_in_calendar_functions = false;
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1990, 2, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1990, 2, 1, 0, 0, 0));
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("DAYS");
    time_unit_column_->append_datum("DAYS");
    calendar_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            // [8:00 am, 5:00 pm]
            R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
            // [8:00 am, 5:00 pm]
            R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} })",
            R"(} })"});
    calendar_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            // [8:00 am, 5:00 pm]
            R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
            R"("friday": {"use_day": false, "shift": {"begin": 0, "end": 0} })",
            R"(} })"});
    calendar_column_->append_datum(
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
    calendar_column_->append_datum(
            DatumArray{
                    R"({"workday_calendar": {)",
                    R"("entries": { "year": 1970, )",
                    celonis::get_is_workdays_str(365, {0, 4, 10, 100, 150}).c_str(),
                    R"(, calendar_id: "id1"},)",
                    R"("entries": { "year": 1970, )",
                    celonis::get_is_workdays_str(365, {0, 4, 10, 100, 364}).c_str(),
                    R"(, calendar_id: "id3"},)",
                    R"("entries": { "year": 1971, )",
                    celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                    R"(, calendar_id: "id1"},)",
                    R"( }})"});
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum("id2");
    calendar_id_column_->append_datum("id2");
    const auto result = Run().value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(660L, result->get(0).get_int64());
    EXPECT_EQ(9L, result->get(1).get_int64());
    EXPECT_EQ(5L, result->get(2).get_int64());
    EXPECT_TRUE(result->get(3).is_null());
    config::treat_calendar_column_as_constant_in_calendar_functions = treat_calendar_column_as_constant_in_calendar_functions;
}

TEST_F(CelonisRemapTimestampsCalendarTest, treat_calendar_column_as_constant_works) {
    const bool treat_calendar_column_as_constant_in_calendar_functions = config::treat_calendar_column_as_constant_in_calendar_functions;
    config::treat_calendar_column_as_constant_in_calendar_functions = true;
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum("MINUTES");
    calendar_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            // [8:00 am, 5:00 pm]
            R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
            // [8:00 am, 5:00 pm]
            R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} })",
            R"(} })"});
    calendar_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            // [8:00 am, 5:00 pm]
            R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
            R"("friday": {"use_day": false, "shift": {"begin": 0, "end": 0} })",
            R"(} })"});
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = Run().value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(660L, result->get(0).get_int64());
    EXPECT_EQ(660L, result->get(1).get_int64());
    config::treat_calendar_column_as_constant_in_calendar_functions = treat_calendar_column_as_constant_in_calendar_functions;
}

TEST_F(CelonisRemapTimestampsCalendarTest, null_column) {
    Prepare();
    timestamp_column_->append_datum(kNullDatum);
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1990, 2, 1, 0, 0, 0));
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum(kNullDatum);
    time_unit_column_->append_datum("DAYS");
    calendar_column_->append_datum(DatumArray{});
    calendar_column_->append_datum(DatumArray{});
    calendar_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum("id2");
    const auto result = Run().value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
}

TEST_F(CelonisRemapTimestampsCalendarTest, calendar_id_is_ignored) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id1");
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(9L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum("id1");
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": )",
                                                 R"({ "entries": { "year": 1970, )",
                                                 celonis::get_is_workdays_str(365, {0}).c_str(),
                                                 R"( } }})"}, "HOURS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(24L, result->get(0).get_int64());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, invalid_time_unit) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendarAndTimeUnit({}, "YEARS");
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(),
              "time unit must be one of WORKDAYS/DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
}

TEST_F(CelonisRemapTimestampsCalendarTest, malformed_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = RunConstantCalendarAndTimeUnit();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Calendar array can not contain null values.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Calendar array can not contain null values.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"weekday_calendar": {)",
                                                 R"("unknown_day": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                                                 R"(} })"}, "HOURS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "[prepare] Calendar specification column is malformed.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                R"("unknown_day": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"});
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Calendar specification column is malformed.");
    }
    // mixed calendar_id entry and no-calendar_id entry
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(DatumArray{R"({"factory_calendar": {)",
                                                  R"("entries": {"start_date": -11, "end_date": -1, "calendar_id": "id1"}, )",
                                                  R"("entries": {"start_date": -40, "end_date": -20, "calendar_id": "id2"}, )",
                                                  R"("entries": {"start_date": -300, "end_date": -20}, )",
                                                  R"( }})"});
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "In FactoryCalendar, ensure that the calendar_id is either set or not set in all entries.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(DatumArray{
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
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "In WorkdayCalendar, ensure that the calendar_id is either set or not set in all entries.");
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, invalid_factory_calendar_entries_are_ignored) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    calendar_id_column_->append_datum("id1");
    const auto result = RunConstantCalendarAndTimeUnit({R"({"factory_calendar": {)",
                                                    // missing start_date
                                             R"("entries": {"end_date": 2000, "calendar_id": "id1"}, )",
                                                    // missing end_date
                                             R"("entries": {"start_date": 3000,"calendar_id": "id1"}, )",
                                                    // start_date >end_date
                                             R"("entries": {"start_date": 8000, "end_date": 6000, "calendar_id": "id1"}, )",
                                             R"("entries": {"start_date": 1000, "end_date": 6000, "calendar_id": "id1"}, )",
                                             R"( }})"}, "MILLISECONDS").value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(5000L, result->get(0).get_int64());
}

TEST_F(CelonisRemapTimestampsCalendarTest, calendar_id_provided_when_not_needed) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    time_unit_column_->append_datum("MILLISECONDS");
    calendar_column_->append_datum(DatumArray{});
    calendar_id_column_->append_datum("CalendarID");
    const auto result = Run();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(),
              "Calendar ID column should not be set when calendar specification is not set.");
}

TEST_F(CelonisRemapTimestampsCalendarTest, calendar_id_not_provided_when_needed) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "id"} }})"},
                "MILLISECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit({R"({"workday_calendar": {)",
                                                            R"("entries": { "year": 1970, )",
                                                            celonis::get_is_workdays_str(365,
                                                                                         {0, 4, 10, 100, 150}).c_str(),
                                                            R"(, calendar_id: "id1"},)",
                                                            R"("entries": { "year": 1970, )",
                                                            celonis::get_is_workdays_str(365,
                                                                                         {0, 4, 10, 100, 364}).c_str(),
                                                            R"(, calendar_id: "id2"},)",
                                                            R"("entries": { "year": 1971, )",
                                                            celonis::get_is_workdays_str(365, {5, 7}).c_str(),
                                                            R"(, calendar_id: "id1"},)",
                                                            R"( }})"}, "MILLISECONDS").value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisRemapTimestampsCalendarTest, invalid_weekday_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 0, "end": 1000} }, "calendar_id": "id" )",
                 R"(} })"}, "MILLISECONDS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "calendar_id should not be set in WeekdayCalendar.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": -1, "end": 1000} })",
                 R"(} })"}, "MILLISECONDS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "shift begin is negative in weekday calendar.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 2000, "end": 1000} })",
                 R"(} })"}, "MILLISECONDS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "shift begin is greater than shift end in weekday calendar.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendarAndTimeUnit(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 1000, "end": 86400005} })",
                 R"(} })"}, "MILLISECONDS");
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "shift end is greater than 86400000 milliseconds in weekday calendar.");
    }
}

} // namespace starrocks
