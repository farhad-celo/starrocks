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

class CelonisInCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        timestamp_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        calendar_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
        calendar_id_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisTimeFunctions::in_calendar_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisTimeFunctions::in_calendar_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisTimeFunctions::in_calendar_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisTimeFunctions::in_calendar_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisTimeFunctions::in_calendar(ctx_.get(),
                                                   {timestamp_column_, calendar_column_, calendar_id_column_});
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
        ctx_->set_constant_columns({nullptr, calendar_column_, nullptr});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr timestamp_column_;
    ColumnPtr calendar_column_;
    ColumnPtr calendar_id_column_;
};

TEST_F(CelonisInCalendarTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisInCalendarTest, const_multiple_weekday_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 9, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"multi_weekday_calendar": {)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}},)",
                 R"(} })"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 9, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        ::celonis::accelerator::Calendar calendar_proto;
        google::protobuf::TextFormat::ParseFromString(R"(
        multi_weekday_calendar {
          calendars {
            thursday {
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
        const auto result = RunConstantCalendar({encoded_string}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 9, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 9, 0, 0));
        calendar_id_column_->append_datum("id1");
        calendar_id_column_->append_datum("id2");
        const auto result = RunConstantCalendar(
                {R"({"multi_weekday_calendar": {)",
                 R"("calendars": {"friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000}}, "calendar_id": "id1"},)",
                 R"("calendars": {"thursday": {"use_day": true, "shift": {"begin": 0, "end": 61200000}}, "calendar_id": "id2"},)",
                 R"(} })"}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
}

TEST_F(CelonisInCalendarTest, const_factory_calendar) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 12, 31, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 8, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 17, 0, 0));
    for (auto i = 0; i < timestamp_column_->size(); ++i) {
        calendar_id_column_->append_datum(kNullDatum);
    }
    const auto result = RunConstantCalendar(
            {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"}).value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
}

TEST_F(CelonisInCalendarTest, const_weekday_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 9, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 8, 10, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 8, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 16, 59, 59, 999000));
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 1));
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 7, 59, 59));
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 1));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 7, 0, 0));
        for (auto i = 0; i < timestamp_column_->size(); ++i) {
            calendar_id_column_->append_datum(kNullDatum);
        }
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )", // [8:00 am, 5:00 pm]
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
        EXPECT_EQ(1L, result->get(4).get_int64());
        EXPECT_EQ(0L, result->get(5).get_int64());
        EXPECT_EQ(0L, result->get(6).get_int64());
        EXPECT_EQ(0L, result->get(7).get_int64());
        EXPECT_EQ(0L, result->get(8).get_int64());
        EXPECT_EQ(0L, result->get(9).get_int64());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200001} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
}

TEST_F(CelonisInCalendarTest, null_input) {
    {
        Prepare();
        timestamp_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(
                DatumArray{
                        R"({"workday_calendar": )",
                        R"({ "entries": { "year": 1970, )",
                        celonis::get_is_workdays_str(365, {0}).c_str(),
                        R"( } }})"});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
        calendar_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisInCalendarTest, const_workday_calendar_without_id) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 1));
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendar({R"({"workday_calendar": )",
                                             R"({ "entries": { "year": 1970, )",
                                             celonis::get_is_workdays_str(365, {0}),
                                             R"( } }})"}).value();
    ASSERT_EQ(2, result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
}

TEST_F(CelonisInCalendarTest, const_workday_calendar_with_id) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 12, 1, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 11, 1, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 12, 1, 0, 0));
    calendar_id_column_->append_datum("id1");
    calendar_id_column_->append_datum("id1");
    calendar_id_column_->append_datum("id3");
    const auto result = RunConstantCalendar({
                                                    R"({"workday_calendar": {)",
                                                    R"("entries": { "year": 1970, )",
                                                    celonis::get_is_workdays_str(365, {0, 10, 15}),
                                                    R"(, calendar_id: "id1"},)",
                                                    R"("entries": { "year": 1970, )",
                                                    celonis::get_is_workdays_str(365, {11}),
                                                    R"(, calendar_id: "id2"},)",
                                                    R"( }})"}).value();
    ASSERT_EQ(3, result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
}

TEST_F(CelonisInCalendarTest, const_intersect_calendar) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 11, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 5, 16, 10, 0));
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 3, 12, 5, 0));
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 3, 11, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2017, 1, 3, 11, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2019, 1, 4, 11, 0, 0));
    for (auto i = 0; i < timestamp_column_->size(); ++i) {
        calendar_id_column_->append_datum(kNullDatum);
    }
    const auto result = RunConstantCalendar({R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                                             R"("entries": {"start_date": 1514883600000, "end_date": 1514894400000}, )",
                                             R"("entries": {"start_date": 1514898000000, "end_date": 1514908800000}, )",
                                             R"("entries": {"start_date": 1514970000000, "end_date": 1514980800000}, )",
                                             R"("entries": {"start_date": 1514984400000, "end_date": 1514995200000}, )",
                                             R"("entries": {"start_date": 1515142800000, "end_date": 1515153600000}, )",
                                             R"("entries": {"start_date": 1515157200000, "end_date": 1515168000000}, )",
                                             R"( }}, )",
                                             R"("calendar2": {"weekday_calendar": {)",
                                             R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                             R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                             R"("wednesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                             R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                             R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                             R"(}})",
                                             R"(}})"}).value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisInCalendarTest, const_invalid_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({"UNKNOWN"});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "[prepare] Calendar specification column is malformed.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({
                                                        R"({"workday_calendar": )",
                                                        R"({ "entries": { "year": 1989, )",
                                                        celonis::get_is_workdays_str(366, {0}).c_str(),
                                                        R"( } }})"});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "1989 should have 365 days, however the workday calendar contains 366 is_workday.");
    }
}

TEST_F(CelonisInCalendarTest, non_const_calendar) {
    const bool treat_calendar_column_as_constant_in_calendar_functions = config::treat_calendar_column_as_constant_in_calendar_functions;
    config::treat_calendar_column_as_constant_in_calendar_functions = false;
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                              R"(} })"});
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200001} }, )",
                                              R"(} })"});
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = Run().value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    config::treat_calendar_column_as_constant_in_calendar_functions = treat_calendar_column_as_constant_in_calendar_functions;
}

TEST_F(CelonisInCalendarTest, treat_calendar_column_as_constant_works) {
    const bool treat_calendar_column_as_constant_in_calendar_functions = config::treat_calendar_column_as_constant_in_calendar_functions;
    config::treat_calendar_column_as_constant_in_calendar_functions = true;
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1969, 12, 25, 17, 0, 0));
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                              R"(} })"});
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200001} }, )",
                                              R"(} })"});
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = Run().value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    config::treat_calendar_column_as_constant_in_calendar_functions = treat_calendar_column_as_constant_in_calendar_functions;
}

TEST_F(CelonisInCalendarTest, non_const_invalid_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        calendar_column_->append_datum(DatumArray{kNullDatum});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Calendar array can not contain null values.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        calendar_column_->append_datum(DatumArray{"UNKNOWN"});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Calendar specification column is malformed.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        calendar_column_->append_datum(DatumArray{
                R"({"workday_calendar": )",
                R"({ "entries": { "year": 1989, )",
                celonis::get_is_workdays_str(366, {0}).c_str(),
                R"( } }})"});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "1989 should have 365 days, however the workday calendar contains 366 is_workday.");
    }
}

TEST_F(CelonisInCalendarTest, const_null_calendar_column) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    calendar_column_ = ColumnHelper::create_const_null_column(2);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = Run().value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->only_null());
    EXPECT_TRUE(result->is_constant());
}

} // namespace starrocks
