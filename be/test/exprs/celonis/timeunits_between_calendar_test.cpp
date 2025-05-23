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

class CelonisTimeunitsBetweenCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        from_timestamp_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        to_timestamp_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        time_unit_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        calendar_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
        calendar_id_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisTimeFunctions::timeunits_between_calendar_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::timeunits_between_calendar_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisTimeFunctions::timeunits_between_calendar_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::timeunits_between_calendar_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisTimeFunctions::timeunits_between_calendar(ctx_.get(),
                                                                  {from_timestamp_column_, to_timestamp_column_,
                                                                   time_unit_column_,
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
        calendar_column_ = ConstColumn::create(calendar_column_, from_timestamp_column_->size());
        ctx_->set_constant_columns({nullptr, nullptr, nullptr, calendar_column_, nullptr});
        return Run();
    }

    StatusOr<ColumnPtr> RunConstantCalendar() {
        ctx_->set_constant_columns(
                {nullptr, nullptr, nullptr, ConstColumn::create(calendar_column_, from_timestamp_column_->size()),
                 nullptr});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr from_timestamp_column_;
    ColumnPtr to_timestamp_column_;
    ColumnPtr time_unit_column_;
    ColumnPtr calendar_column_;
    ColumnPtr calendar_id_column_;
};

TEST_F(CelonisTimeunitsBetweenCalendarTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, const_empty_calendar) {
    Prepare();
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 0, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 0, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 1, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2000, 1, 1, 23, 29, 59, 999000));
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 1, 10, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2000, 1, 1, 1, 0, 29, 999000));
    from_timestamp_column_->append_datum(TimestampValue::create(2000, 1, 1, 0, 0, 59, 999000));
    from_timestamp_column_->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 10));
    from_timestamp_column_->append_datum(TimestampValue::create(2000, 1, 1, 0, 0, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 10));

    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 12, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 12, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 5, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 2, 20, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
    to_timestamp_column_->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
    to_timestamp_column_->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 11, 500000));
    to_timestamp_column_->append_datum(TimestampValue::create(1999, 12, 31, 23, 59, 59, 999000));
    to_timestamp_column_->append_datum(TimestampValue::create(2005, 5, 9, 12, 1, 11, 0));

    time_unit_column_->append_datum("WORKDAYS");
    time_unit_column_->append_datum("DAYS");
    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("HOURS");
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum("MINUTES");
    time_unit_column_->append_datum("SECONDS");
    time_unit_column_->append_datum("SECONDS");
    time_unit_column_->append_datum("MILLISECONDS");
    time_unit_column_->append_datum("MILLISECONDS");

    for (auto i = 0; i < from_timestamp_column_->size(); ++i) {
        calendar_id_column_->append_datum(kNullDatum);
    }
    const auto result = RunConstantCalendar({}).value();
    ASSERT_EQ(from_timestamp_column_->size(), result->size());
    EXPECT_EQ(1.0, result->get(0).get_double());
    EXPECT_EQ(1.5, result->get(1).get_double());
    EXPECT_EQ(4.0, result->get(2).get_double());
    EXPECT_EQ(-23.5, result->get(3).get_double());
    EXPECT_EQ(70.0, result->get(4).get_double());
    EXPECT_EQ(-60.5, result->get(5).get_double());
    EXPECT_EQ(-60.0, result->get(6).get_double());
    EXPECT_EQ(1.5, result->get(7).get_double());
    EXPECT_EQ(-1.0, result->get(8).get_double());
    EXPECT_EQ(1000.0, result->get(9).get_double());
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, const_weekday_calendar) {
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 2, 0, 0));
        time_unit_column_->append_datum("DAYS");
        time_unit_column_->append_datum("DAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({
                                                        R"({"weekday_calendar": {)",
                                                        // [8:00 am, 5:00 pm]
                                                        R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("saturday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"(} })"}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_EQ(1.125, result->get(0).get_double());
        EXPECT_EQ(-1.875, result->get(1).get_double());
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("DAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 28800000, "end_date": 61200000 })",
                                                 R"(} })"}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_EQ(1.0, result->get(0).get_double());
        EXPECT_EQ(0.375, result->get(1).get_double());
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        from_timestamp_column_->append_datum(TimestampValue::create(10000, 1, 1, 1, 1, 0)); // invalid
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 2, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1399, 12, 31, 1, 0, 0)); // invalid
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({
                                                        R"({"weekday_calendar": {)",
                                                        // [8:00 am, 5:00 pm]
                                                        R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("tuesday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("thursday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("friday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"("saturday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                                        R"(} })"}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_EQ(3.0, result->get(0).get_double());
        EXPECT_EQ(-5.0, result->get(1).get_double());
        EXPECT_TRUE(result->get(2).is_null());
        EXPECT_TRUE(result->get(3).is_null());
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 8, 2, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 1, 2, 0, 0));
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("WORKDAYS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
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
        const auto result = RunConstantCalendar({encoded_string.c_str()}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_EQ(3.0, result->get(0).get_double());
        EXPECT_EQ(-5.0, result->get(1).get_double());
    }
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, const_intersect_calendar) {
    Prepare();
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 9, 0, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
    time_unit_column_->append_datum("WORKDAYS");
    time_unit_column_->append_datum("WORKDAYS");
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendar({R"({"intersect_calendar": {"calendar1": {"factory_calendar": {)",
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
    ASSERT_EQ(from_timestamp_column_->size(), result->size());
    EXPECT_EQ(2.0, result->get(0).get_double());
    EXPECT_EQ(-2.0, result->get(1).get_double());
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, null_id_when_id_is_required) {
    Prepare();
    time_unit_column_->append_datum("MILLISECONDS");
    time_unit_column_->append_datum("MILLISECONDS");
    from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    calendar_id_column_->append_datum("id1");
    calendar_id_column_->append_datum(kNullDatum);
    const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                             R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "id1"}, )",
                                             R"("entries": {"start_date": 1514768400000, "end_date": 1515546000000, "calendar_id": "id2"})",
                                             R"(} })"}).value();
    ASSERT_EQ(from_timestamp_column_->size(), result->size());
    EXPECT_EQ(1000, result->get(0).get_double());
    EXPECT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, outside_of_scope) {
    {
        Prepare();
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("DAYS");
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_unit_column_->size(); ++i) {
            from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendar_id_column_->append_datum(kNullDatum);
        }
        const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 0, "end_date": 1000 })",
                                                 R"(} })"}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        for (auto i = 0; i < from_timestamp_column_->size(); ++i) {
            EXPECT_TRUE(result->get(i).is_null());
        }
    }
    {
        Prepare();
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("DAYS");
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_unit_column_->size(); ++i) {
            from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendar_id_column_->append_datum("id1");
        }
        const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "id1"}, )",
                                                 R"("entries": {"start_date": 1514768400000, "end_date": 1515546000000, "calendar_id": "id2"})",
                                                 R"(} })"}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        for (auto i = 0; i < from_timestamp_column_->size(); ++i) {
            EXPECT_TRUE(result->get(i).is_null());
        }
    }
    {
        Prepare();
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("DAYS");
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_unit_column_->size(); ++i) {
            from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendar_id_column_->append_datum("id2");
        }
        const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "id1"}, )",
                                                 R"("entries": {"start_date": 1514768400000, "end_date": 1515546000000, "calendar_id": "id2"})",
                                                 R"(} })"}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        for (auto i = 0; i < from_timestamp_column_->size(); ++i) {
            EXPECT_FALSE(result->get(i).is_null());
        }
    }
    {
        Prepare();
        time_unit_column_->append_datum("WORKDAYS");
        time_unit_column_->append_datum("DAYS");
        time_unit_column_->append_datum("HOURS");
        time_unit_column_->append_datum("MINUTES");
        time_unit_column_->append_datum("SECONDS");
        time_unit_column_->append_datum("MILLISECONDS");
        for (auto i = 0; i < time_unit_column_->size(); ++i) {
            from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
            to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 0, 0, 0));
            calendar_id_column_->append_datum("id");
        }
        const auto result = RunConstantCalendar({R"({"factory_calendar": {)",
                                                 R"("entries": {"start_date": 0, "end_date": 1000, "calendar_id": "id1"}, )",
                                                 R"("entries": {"start_date": 1514768400000, "end_date": 1515546000000, "calendar_id": "id2"})",
                                                 R"(} })"}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        for (auto i = 0; i < from_timestamp_column_->size(); ++i) {
            EXPECT_EQ(0.0, result->get(i).get_double());
        }
    }
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, null_input) {
    Prepare();
    from_timestamp_column_->append_datum(kNullDatum);
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
    from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
    to_timestamp_column_->append_datum(kNullDatum);
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
    to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
    time_unit_column_->append_datum("DAYS");
    time_unit_column_->append_datum("DAYS");
    time_unit_column_->append_datum(kNullDatum);
    time_unit_column_->append_datum("DAYS");
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                              R"(} })"});
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                              R"(} })"});
    calendar_column_->append_datum(DatumArray{R"({"weekday_calendar": {)",
                                              R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                                              R"(} })"});
    calendar_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);
    calendar_id_column_->append_datum(kNullDatum);

    const auto result = Run().value();
    for (auto i = 0; i < from_timestamp_column_->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, invalid_input) {
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_unit_column_->append_datum("DAYS");
        calendar_column_->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                kNullDatum,
                // [8:00 am, 5:00 pm]
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"(} })"});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Calendar array can not contain null values.");
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_unit_column_->append_datum("UNKNOWN");
        calendar_column_->append_datum(DatumArray{
                R"({"weekday_calendar": {)",
                // [8:00 am, 5:00 pm]
                R"("monday": {"use_day": true, "shift": {"begin": 28800000, "end": 61200000} }, )",
                R"(} })"});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "time unit must be one of WORKDAYS/DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_column_->append_datum(DatumArray{kNullDatum});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Calendar array can not contain null values.");
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 2, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(2018, 1, 6, 1, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_column_->append_datum(DatumArray{"unknown"});
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "[prepare] Calendar specification column is malformed.");
    }
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, empty_calendar) {
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar({}).value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_EQ(33.0, result->get(0).get_double());
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 10, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_id_column_->append_datum(kNullDatum);
        calendar_column_->append_datum(DatumArray{});
        const auto result = Run().value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_EQ(33.0, result->get(0).get_double());
    }
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, null_calendar) {
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar().value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 1, 10, 0, 0));
        time_unit_column_->append_datum("HOURS");
        calendar_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = Run().value();
        ASSERT_EQ(from_timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTimeunitsBetweenCalendarTest, year_gaps_in_workday_calendar) {
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 6, 1, 0, 0));
        time_unit_column_->append_datum("DAYS");
        calendar_column_->append_datum(DatumArray{
                R"({"workday_calendar": {)",
                R"("entries": { "year": 1970, )",
                celonis::get_workday_mask_str(365, {0, 10, 15}).c_str(),
                R"(, calendar_id: "id1"},)",
                R"("entries": { "year": 1972, )",
                celonis::get_workday_mask_str(366, {11}).c_str(),
                R"(, calendar_id: "id1"},)",
                R"( }})"});
        calendar_id_column_->append_datum("id1");
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "Year gaps are found in the workday calendar configuration.");
    }
    {
        Prepare();
        from_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 2, 1, 0, 0));
        to_timestamp_column_->append_datum(TimestampValue::create(1970, 1, 6, 1, 0, 0));
        time_unit_column_->append_datum("DAYS");
        calendar_column_->append_datum(DatumArray{
                R"({"workday_calendar": {)",
                R"("entries": { "year": 1970, )",
                celonis::get_is_workdays_str(365, {0, 10, 15}).c_str(),
                R"(, calendar_id: "id1"},)",
                R"("entries": { "year": 1972, )",
                celonis::get_is_workdays_str(366, {11}).c_str(),
                R"(, calendar_id: "id2"},)",
                R"( }})"});
        calendar_id_column_->append_datum("id1");
        const auto result = Run();
        ASSERT_TRUE(result.status().ok());
    }
}

} // namespace starrocks
