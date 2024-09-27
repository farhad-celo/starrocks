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

class CelonisAddTimeunitsCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        timestamp_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        add_value_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        time_unit_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        calendar_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
        calendar_id_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisTimeFunctions::add_timeunits_calendar_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::add_timeunits_calendar_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisTimeFunctions::add_timeunits_calendar_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::add_timeunits_calendar_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisTimeFunctions::add_timeunits_calendar(ctx_.get(),
                                                              {timestamp_column_, add_value_column_, time_unit_column_,
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
        ctx_->set_constant_columns({nullptr, nullptr, nullptr, calendar_column_, nullptr});
        return Run();
    }

    StatusOr<ColumnPtr> RunConstantCalendar() {
        ctx_->set_constant_columns(
                {nullptr, nullptr, nullptr, ConstColumn::create(calendar_column_, timestamp_column_->size()), nullptr});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr timestamp_column_;
    ColumnPtr add_value_column_;
    ColumnPtr time_unit_column_;
    ColumnPtr calendar_column_;
    ColumnPtr calendar_id_column_;
};

TEST_F(CelonisAddTimeunitsCalendarTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisAddTimeunitsCalendarTest, const_empty_calendar) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 10, 10, 1, 2));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 10, 10, 1, 2));
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 10, 10, 1, 2));

    add_value_column_->append_datum(26L);
    add_value_column_->append_datum(365L);
    add_value_column_->append_datum(-5L);
    add_value_column_->append_datum(120L);
    add_value_column_->append_datum(-3662L);

    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("DAYS");
    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum("SECONDS");

    for (auto i = 0; i < timestamp_column_->size(); ++i) {
        calendar_id_column_->append_datum(kNullDatum);
    }
    const auto result = RunConstantCalendar({}).value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(TimestampValue::create(1970, 1, 2, 4, 0, 0), result->get(0).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1971, 1, 1, 2, 0, 0), result->get(1).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1970, 1, 10, 5, 1, 2), result->get(2).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1970, 1, 10, 12, 1, 2), result->get(3).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1970, 1, 10, 9, 0, 0), result->get(4).get_timestamp());
}

TEST_F(CelonisAddTimeunitsCalendarTest, const_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 9, 0, 0));
        add_value_column_->append_datum(17L);
        add_value_column_->append_datum(-5L);
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 4, 11, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 6, 12, 0, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, add_workdays) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 1, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        add_value_column_->append_datum(4L);
        add_value_column_->append_datum(-4L);
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 6, 1, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 2, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 1, 0, 0));
        add_value_column_->append_datum(0L);
        time_unit_column_->append_datum("WORKDAYS");
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 1, 0, 0), result->get(0).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 9, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2023, 1, 1, 0, 0, 0));
        add_value_column_->append_datum(2L);
        add_value_column_->append_datum(-2L);
        add_value_column_->append_datum(1L);
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
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
                 R"(}})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 6, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 6, 0, 0, 0), result->get(1).get_timestamp());
        EXPECT_TRUE(result->get(2).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 3, 0, 0, 0));
        add_value_column_->append_datum(2L);
        add_value_column_->append_datum(2L);
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1515052800000, "end_date": 1515085200000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1515225600000, "end_date": 1515258000000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1514966400000, "end_date": 1514998800000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515312000000, "end_date": 1515326400000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515398400000, "end_date": 1515430800000, "calendar_id": "US"}, )",
                 R"( }})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 6, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 8, 0, 0, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, add_days) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 3, 0, 0, 0));
    add_value_column_->append_datum(3650000L);
    add_value_column_->append_datum(-3650000L);
    time_unit_column_->append_datum("DAYS");
    time_unit_column_->append_datum("DAYS");
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendar(
            {R"({"weekday_calendar": {)",
             R"("tuesday": {"use_day": true, "shift": {"begin": 0, "end": 61200000} }, )",
             R"(} })"}).value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisAddTimeunitsCalendarTest, add_hours) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 9, 0, 0));
        add_value_column_->append_datum(17L);
        add_value_column_->append_datum(-5L);
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 4, 11, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 6, 12, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 5, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 9, 0, 0));
        add_value_column_->append_datum(0L);
        add_value_column_->append_datum(0L);
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2023, 1, 2, 0, 0, 0));
        add_value_column_->append_datum(20L);
        add_value_column_->append_datum(20L);
        add_value_column_->append_datum(20L);
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1514966400000, "end_date": 1514998800000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1515139200000, "end_date": 1515171600000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1515247200000, "end_date": 1515279600000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515333600000, "end_date": 1515366000000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515420000000, "end_date": 1515452400000, "calendar_id": "US"}, )",
                 R"( }})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 5, 10, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 8, 16, 0, 0), result->get(1).get_timestamp());
        EXPECT_TRUE(result->get(2).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 11, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 5, 16, 10, 0));
        add_value_column_->append_datum(3L);
        add_value_column_->append_datum(-16L);
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
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
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 15, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 11, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        add_value_column_->append_datum(20L);
        add_value_column_->append_datum(20L);
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum("JP");
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar(
                {R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1514966400000, "end_date": 1514998800000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1515139200000, "end_date": 1515171600000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1515247200000, "end_date": 1515279600000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515333600000, "end_date": 1515366000000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515420000000, "end_date": 1515452400000, "calendar_id": "US"}, )",
                 R"( }}, )",
                 R"("calendar2": {"factory_calendar": {)",
                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "JP"}, )",
                 R"("entries": {"start_date": 1514966400000, "end_date": 1514998800000, "calendar_id": "JP"}, )",
                 R"("entries": {"start_date": 1515139200000, "end_date": 1515171600000, "calendar_id": "JP"}, )",
                 R"("entries": {"start_date": 1515247200000, "end_date": 1515279600000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515333600000, "end_date": 1515366000000, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 1515420000000, "end_date": 1515452400000, "calendar_id": "US"}, )",
                 R"(}})",
                 R"(}})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_EQ(TimestampValue::create(2018, 1, 8, 16, 0, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, add_minutes) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 9, 0, 0));
        add_value_column_->append_datum(61L);
        add_value_column_->append_datum(-5L);
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("MINUTES");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 11, 1, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 16, 55, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 5, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 9, 0, 0));
        add_value_column_->append_datum(0L);
        add_value_column_->append_datum(0L);
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("MINUTES");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        add_value_column_->append_datum(61L);
        add_value_column_->append_datum(62L);
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("MINUTES");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "DE"}, )",
                                                 R"("entries": {"start_date": 1514901600000, "end_date": 1514934000000, "calendar_id": "US"}, )",
                                                 R"( }})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 9, 1, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 15, 2, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        add_value_column_->append_datum(0L);
        time_unit_column_->append_datum("MINUTES");
        calendar_id_column_->append_datum("JP");
        const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "DE"}, )",
                                                 R"("entries": {"start_date": 1514901600000, "end_date": 1514934000000, "calendar_id": "US"}, )",
                                                 R"( }})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 0, 0, 0), result->get(0).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        add_value_column_->append_datum(61L);
        add_value_column_->append_datum(62L);
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("MINUTES");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        ::celonis::accelerator::Calendar calendar_proto;
        google::protobuf::TextFormat::ParseFromString(R"(
        factory_calendar {
          entries {
            start_date: 1514880000000
            end_date: 1514912400000
            calendar_id: "DE"
          }
          entries {
            start_date: 1514901600000
            end_date: 1514934000000
            calendar_id: "US"
          }
        }
        )", &calendar_proto);
        std::string encoded_string = celonis::to_base64_encoded_string(calendar_proto);
        const auto result = RunConstantCalendar({encoded_string}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 9, 1, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 15, 2, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 11, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 3, 9, 10, 0));
        add_value_column_->append_datum(62L);
        add_value_column_->append_datum(-20L);
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("MINUTES");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                 R"("entries": {"start_date": 1514883600000, "end_date": 1514894400000}, )",
                 R"("entries": {"start_date": 1514898000000, "end_date": 1514908800000}, )",
                 R"("entries": {"start_date": 1514970000000, "end_date": 1514980800000}, )",
                 R"("entries": {"start_date": 1514984400000, "end_date": 1514995200000}, )",
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
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 13, 2, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 15, 50, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, add_seconds) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        add_value_column_->append_datum(0L);
        time_unit_column_->append_datum("SECONDS");
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": false, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 10, 0, 0), result->get(0).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 9, 0, 0));
        add_value_column_->append_datum(61L);
        add_value_column_->append_datum(-5L);
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("SECONDS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 10, 1, 1), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 16, 59, 55), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 5, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 9, 0, 0));
        add_value_column_->append_datum(0L);
        add_value_column_->append_datum(0L);
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("SECONDS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        add_value_column_->append_datum(61L);
        add_value_column_->append_datum(62L);
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("SECONDS");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1514901600000, "end_date": 1514934000000, "calendar_id": "US"}, )",
                 R"( }})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 8, 1, 1), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 14, 1, 2), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 11, 58, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 3, 9, 0, 10));
        add_value_column_->append_datum(122L);
        add_value_column_->append_datum(-20L);
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("SECONDS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                 R"("entries": {"start_date": 1514883600000, "end_date": 1514894400000}, )",
                 R"("entries": {"start_date": 1514898000000, "end_date": 1514908800000}, )",
                 R"("entries": {"start_date": 1514970000000, "end_date": 1514980800000}, )",
                 R"("entries": {"start_date": 1514984400000, "end_date": 1514995200000}, )",
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
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 13, 0, 2), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 15, 59, 50), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, add_millis) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 9, 0, 0));
        add_value_column_->append_datum(1111L);
        add_value_column_->append_datum(-5L);
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 10, 0, 1, 111000), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 16, 59, 59, 995000), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 5, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 9, 0, 0));
        add_value_column_->append_datum(0L);
        add_value_column_->append_datum(0L);
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"weekday_calendar": {)",
                 R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                 R"(} })"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 9, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        add_value_column_->append_datum(0L);
        add_value_column_->append_datum(1000L);
        add_value_column_->append_datum(1000L);
        add_value_column_->append_datum(-5L);
        add_value_column_->append_datum(1000L);
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        calendar_id_column_->append_datum("JP");
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "JP"}, )",
                 R"("entries": {"start_date": 2000, "end_date": 3000, "calendar_id": "JP"}, )",
                 R"("entries": {"start_date": 0, "end_date": 1001, "calendar_id": "US"}, )",
                 R"( }})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(1970, 1, 1, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(1970, 1, 1, 0, 0, 1), result->get(1).get_timestamp());
        EXPECT_TRUE(result->get(2).is_null());
        EXPECT_TRUE(result->get(3).is_null());
        EXPECT_EQ(TimestampValue::create(1970, 1, 1, 0, 0, 2), result->get(4).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        add_value_column_->append_datum(1001L);
        add_value_column_->append_datum(1002L);
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        calendar_id_column_->append_datum("DE");
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 1514880000000, "end_date": 1514912400000, "calendar_id": "DE"}, )",
                 R"("entries": {"start_date": 1514901600000, "end_date": 1514934000000, "calendar_id": "US"}, )",
                 R"( }})"}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 8, 0, 1, 1000), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 14, 0, 1, 2000), result->get(1).get_timestamp());
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 11, 59, 59, 900000));
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 3, 9, 0, 0, 10000));
        add_value_column_->append_datum(100L);
        add_value_column_->append_datum(-20L);
        time_unit_column_->append_datum("MILLISECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
                 R"("entries": {"start_date": 1514883600000, "end_date": 1514894400000}, )",
                 R"("entries": {"start_date": 1514898000000, "end_date": 1514908800000}, )",
                 R"("entries": {"start_date": 1514970000000, "end_date": 1514980800000}, )",
                 R"("entries": {"start_date": 1514984400000, "end_date": 1514995200000}, )",
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
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 13, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 15, 59, 59, 990000), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, const_null_calendar) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
    add_value_column_->append_datum(10L);
    time_unit_column_->append_datum("HOURS");
    calendar_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendar().value();
    ASSERT_EQ(1, result->size());
    EXPECT_TRUE(result->get(0).is_null());
}

TEST_F(CelonisAddTimeunitsCalendarTest, invalid_time_unit) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        add_value_column_->append_datum(10L);
        time_unit_column_->append_datum("UNKNOWN");
        calendar_column_->append_datum(DatumArray{});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "time unit must be one of WORKDAYS/DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        add_value_column_->append_datum(10L);
        time_unit_column_->append_datum("UNKNOWN");
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "time unit must be one of WORKDAYS/DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, const_malformed_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        add_value_column_->append_datum(10L);
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({"UNKNOWN"});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "[prepare] Calendar specification column is malformed.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        add_value_column_->append_datum(10L);
        time_unit_column_->append_datum("HOURS");
        calendar_column_->append_datum(DatumArray{kNullDatum});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar array can not contain null values.");
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, non_const_malformed_calendar) {
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        add_value_column_->append_datum(10L);
        time_unit_column_->append_datum("HOURS");
        calendar_column_->append_datum(DatumArray{"UNKNOWN"});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar specification column is malformed.");
    }
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
        add_value_column_->append_datum(10L);
        time_unit_column_->append_datum("HOURS");
        calendar_column_->append_datum(DatumArray{kNullDatum});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "Calendar array can not contain null values.");
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, null_input) {
    {
        Prepare();
        timestamp_column_->append_datum(kNullDatum);
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 10, 10, 1, 2));

        add_value_column_->append_datum(26L);
        add_value_column_->append_datum(kNullDatum);
        add_value_column_->append_datum(-5L);

        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("DAYS");
        time_unit_column_->append_datum(kNullDatum);

        for (auto i = 0; i < timestamp_column_->size(); ++i) {
            calendar_id_column_->append_datum(kNullDatum);
        }
        const auto result = RunConstantCalendar({}).value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_TRUE(result->get(2).is_null());
    }
    {
        Prepare();
        timestamp_column_->append_datum(kNullDatum);
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 10, 10, 1, 2));
        timestamp_column_->append_datum(TimestampValue::create(1970, 1, 10, 10, 1, 2));

        add_value_column_->append_datum(26L);
        add_value_column_->append_datum(kNullDatum);
        add_value_column_->append_datum(-5L);
        add_value_column_->append_datum(3600L);

        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("DAYS");
        time_unit_column_->append_datum(kNullDatum);
        time_unit_column_->append_datum("SECONDS");

        calendar_column_->append_datum(DatumArray{});
        calendar_column_->append_datum(DatumArray{});
        calendar_column_->append_datum(DatumArray{});
        calendar_column_->append_datum(kNullDatum);

        for (auto i = 0; i < timestamp_column_->size(); ++i) {
            calendar_id_column_->append_datum(kNullDatum);
        }
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_TRUE(result->get(2).is_null());
        EXPECT_TRUE(result->get(3).is_null());
    }
}

TEST_F(CelonisAddTimeunitsCalendarTest, non_const_calendar) {
    const bool treat_calendar_column_as_constant_in_calendar_functions = config::treat_calendar_column_as_constant_in_calendar_functions;
    config::treat_calendar_column_as_constant_in_calendar_functions = false;
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));

    add_value_column_->append_datum(26L);
    add_value_column_->append_datum(17L);

    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("HOURS");

    calendar_column_->append_datum(DatumArray{});
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"(} })"});

    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);

    const auto result = Run().value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(TimestampValue::create(1970, 1, 2, 4, 0, 0), result->get(0).get_timestamp());
    EXPECT_EQ(TimestampValue::create(2018, 1, 4, 11, 0, 0), result->get(1).get_timestamp());
    config::treat_calendar_column_as_constant_in_calendar_functions = treat_calendar_column_as_constant_in_calendar_functions;
}

TEST_F(CelonisAddTimeunitsCalendarTest, treat_calendar_column_as_constant_works) {
    const bool treat_calendar_column_as_constant_in_calendar_functions = config::treat_calendar_column_as_constant_in_calendar_functions;
    config::treat_calendar_column_as_constant_in_calendar_functions = true;
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));

    add_value_column_->append_datum(26L);
    add_value_column_->append_datum(17L);

    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("HOURS");

    calendar_column_->append_datum(DatumArray{});
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("monday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("tuesday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("thursday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("friday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"("saturday": {"use_day": true, "shift": {"begin": 32400000, "end": 61200000} }, )",
                                              R"(} })"});

    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);

    const auto result = Run().value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(TimestampValue::create(1970, 1, 2, 4, 0, 0), result->get(0).get_timestamp());
    EXPECT_EQ(TimestampValue::create(2018, 1, 2, 3, 0, 0), result->get(1).get_timestamp());
    config::treat_calendar_column_as_constant_in_calendar_functions = treat_calendar_column_as_constant_in_calendar_functions;
}

TEST_F(CelonisAddTimeunitsCalendarTest, const_null_calendar_column) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 10, 0, 0));
    add_value_column_->append_datum(26L);
    add_value_column_->append_datum(17L);
    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("HOURS");
    calendar_column_ = ColumnHelper::create_const_null_column(2);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = Run().value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->only_null());
    EXPECT_TRUE(result->is_constant());
}

} // namespace starrocks
