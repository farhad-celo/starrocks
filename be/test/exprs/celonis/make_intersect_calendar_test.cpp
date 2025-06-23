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

class CelonisMakeIntersectCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        calendar1_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
        calendar2_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
    }

    StatusOr<ColumnPtr> Run() {
        std::vector<ColumnPtr> const_columns;
        const_columns.push_back(calendar1_column_->is_constant() ? calendar1_column_ : nullptr);
        const_columns.push_back(calendar2_column_->is_constant() ? calendar2_column_ : nullptr);
        DeferOp close_fragment_local([this] {
            CelonisTimeFunctions::make_intersect_calendar_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::make_intersect_calendar_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisTimeFunctions::make_intersect_calendar_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::make_intersect_calendar_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisTimeFunctions::make_intersect_calendar(ctx_.get(), {calendar1_column_, calendar2_column_});
        return result;
    }

    StatusOr<ColumnPtr>
    RunConstantCalendars(const std::optional<DatumArray>& calendar1_array,
                         const std::optional<DatumArray>& calendar2_array, size_t num_rows) {
        if (calendar1_array.has_value()) {
            calendar1_column_->append_datum(calendar1_array.value());
            calendar1_column_ = ConstColumn::create(calendar1_column_, num_rows);
        } else {
            calendar1_column_ = ColumnHelper::create_const_null_column(num_rows);
        }
        if (calendar2_array.has_value()) {
            calendar2_column_->append_datum(calendar2_array.value());
            calendar2_column_ = ConstColumn::create(calendar2_column_, num_rows);
        } else {
            calendar2_column_ = ColumnHelper::create_const_null_column(num_rows);
        }
        ctx_->set_constant_columns({calendar1_column_, calendar2_column_});
        return Run();
    }

    void ValidateRow(const ColumnPtr& result, size_t row, const std::optional<std::string>& expected_string) {
        ASSERT_TRUE(row < result->size());
        if (expected_string.has_value()) {
            std::string calendar_str;
            auto array = result->get(row).get_array();
            for (auto item: array) {
                calendar_str += item.get_slice().to_string();
            }
            auto json_string = celonis::to_calendar_json_string(calendar_str);
            ASSERT_TRUE(json_string.has_value());
            EXPECT_EQ(expected_string.value(), json_string.value());
        } else {
            EXPECT_TRUE(result->is_null(row));
        }
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr calendar1_column_;
    ColumnPtr calendar2_column_;
};

TEST_F(CelonisMakeIntersectCalendarTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisMakeIntersectCalendarTest, const_null_calendar1) {
    Prepare();
    DatumArray calendar_array = DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    const auto result = RunConstantCalendars(std::nullopt, calendar_array, 2).value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->only_null());
    EXPECT_TRUE(result->is_constant());
}

TEST_F(CelonisMakeIntersectCalendarTest, const_null_calendar2) {
    Prepare();
    DatumArray calendar_array = DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    const auto result = RunConstantCalendars(calendar_array, std::nullopt, 2).value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->only_null());
    EXPECT_TRUE(result->is_constant());
}

TEST_F(CelonisMakeIntersectCalendarTest, const_null_calendar1_and_calendar2) {
    Prepare();
    const auto result = RunConstantCalendars(std::nullopt, std::nullopt, 2).value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->only_null());
    EXPECT_TRUE(result->is_constant());
}

TEST_F(CelonisMakeIntersectCalendarTest, const_input) {
    {
        Prepare();
        DatumArray calendar1_array = DatumArray{
                R"({"weekday_calendar": {)",
                R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
                R"(} })"};
        DatumArray calendar2_array = DatumArray{
                R"({"weekday_calendar": {)",
                R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 2000} })",
                R"(} })"};
        const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
        ASSERT_EQ(1, result->size());
        ASSERT_TRUE(result->is_constant());
        ValidateRow(result, 0,
                    R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":2000}}}}}})");
    }
    {
        Prepare();
        DatumArray calendar1_array = DatumArray{
                R"({"multiWeekdayCalendar":{"calendars":[{"saturday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"US"},{"wednesday":{"useDay":true,"shift":{"begin":1,"end":1000}},"friday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"DE"}]}})"
        };
        DatumArray calendar2_array = DatumArray{
                R"({"multiWeekdayCalendar":{"calendars":[{"monday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"US"},{"tuesday":{"useDay":true,"shift":{"begin":1,"end":1000}},"friday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"JPN"}]}})"
        };
        const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
        ASSERT_EQ(1, result->size());
        ASSERT_TRUE(result->is_constant());
        ValidateRow(result, 0,
                    R"({"intersectCalendar":{"calendar1":{"multiWeekdayCalendar":{"calendars":[{"saturday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"US"},{"wednesday":{"useDay":true,"shift":{"begin":1,"end":1000}},"friday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"DE"}]}},"calendar2":{"multiWeekdayCalendar":{"calendars":[{"monday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"US"},{"tuesday":{"useDay":true,"shift":{"begin":1,"end":1000}},"friday":{"useDay":true,"shift":{"begin":0,"end":1000}},"calendarId":"JPN"}]}}}})");
    }
}

TEST_F(CelonisMakeIntersectCalendarTest, const_calendar1) {
    Prepare();
    calendar1_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar2_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar1_column_ = ConstColumn::create(calendar1_column_, calendar1_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0,
                R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
}

TEST_F(CelonisMakeIntersectCalendarTest, const_calendar2) {
    Prepare();
    calendar1_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar2_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar2_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0,
                R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
}

TEST_F(CelonisMakeIntersectCalendarTest, multiple_rows) {
    Prepare();
    calendar1_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar1_column_->append_datum(kNullDatum);
    calendar1_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 2000} })",
            R"(} })"});
    calendar2_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar1_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(3, result->size());
    ValidateRow(result, 0,
                R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
    ValidateRow(result, 1, std::nullopt);
    ValidateRow(result, 2,
                R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":2000}}}},"calendar2":{"weekdayCalendar":{"friday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
}

TEST_F(CelonisMakeIntersectCalendarTest, null_calendar1) {
    Prepare();
    calendar1_column_->append_datum(kNullDatum);
    calendar2_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar1_column_ = ConstColumn::create(calendar1_column_, calendar1_column_->size());
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar2_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, malformed_calendar1) {
    Prepare();
    calendar1_column_->append_datum(DatumArray{"Unknown"});
    calendar2_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar1_column_ = ConstColumn::create(calendar1_column_, calendar1_column_->size());
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar2_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, null_calendar2) {
    Prepare();
    calendar2_column_->append_datum(kNullDatum);
    calendar1_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar1_column_ = ConstColumn::create(calendar1_column_, calendar1_column_->size());
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar2_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, malformed_calendar2) {
    Prepare();
    calendar2_column_->append_datum(DatumArray{"Unknown"});
    calendar1_column_->append_datum(DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"});
    calendar1_column_ = ConstColumn::create(calendar1_column_, calendar1_column_->size());
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar2_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, null_calendar1_and_calendar2) {
    Prepare();
    calendar1_column_->append_datum(kNullDatum);
    calendar2_column_->append_datum(kNullDatum);
    calendar1_column_ = ConstColumn::create(calendar1_column_, calendar1_column_->size());
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar2_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, malformed_calendar1_and_calendar2) {
    Prepare();
    calendar1_column_->append_datum(DatumArray{"Unknown"});
    calendar2_column_->append_datum(DatumArray{"Unknown"});
    calendar1_column_ = ConstColumn::create(calendar1_column_, calendar1_column_->size());
    calendar2_column_ = ConstColumn::create(calendar2_column_, calendar2_column_->size());
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, long_calendar) {
    Prepare();
    const size_t n_entries = 20000;
    DatumArray array;
    array.emplace_back(R"({"factory_calendar": {)");
    for (size_t i = 0; i < n_entries; ++i) {
        array.emplace_back(R"("entries": {"start_date": -86400000, "end_date": 3600000, "calendar_id": "id1" }, )");
    }
    array.emplace_back(R"(} })");
    const auto result = RunConstantCalendars(array, array, 2).value();

    ASSERT_EQ(1, result->size());
    EXPECT_EQ(2L, result->get(0).get_array().size());
}

TEST_F(CelonisMakeIntersectCalendarTest, empty_calendar1) {
    Prepare();
    DatumArray calendar1_array = DatumArray{};
    DatumArray calendar2_array = DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0,
                R"({"intersectCalendar":{"calendar1":{},"calendar2":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}}}})");
}

TEST_F(CelonisMakeIntersectCalendarTest, empty_calendar2) {
    Prepare();
    DatumArray calendar1_array = DatumArray{
            R"({"weekday_calendar": {)",
            R"("thursday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    DatumArray calendar2_array = DatumArray{};
    const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0,
                R"({"intersectCalendar":{"calendar1":{"weekdayCalendar":{"thursday":{"useDay":true,"shift":{"begin":0,"end":1000}}}},"calendar2":{}}})");
}

TEST_F(CelonisMakeIntersectCalendarTest, empty_calendar1_and_calendar2) {
    Prepare();
    DatumArray calendar1_array = DatumArray{};
    DatumArray calendar2_array = DatumArray{};
    const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, R"({"intersectCalendar":{"calendar1":{},"calendar2":{}}})");
}

TEST_F(CelonisMakeIntersectCalendarTest, null_value_in_calendar_1_array) {
    Prepare();
    DatumArray calendar1_array = DatumArray{
            R"({"weekday_calendar": {)",
            kNullDatum,
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    DatumArray calendar2_array = DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, null_value_in_calendar_2_array) {
    Prepare();
    DatumArray calendar1_array = DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    DatumArray calendar2_array = DatumArray{
            R"({"weekday_calendar": {)",
            kNullDatum,
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

TEST_F(CelonisMakeIntersectCalendarTest, null_value_in_calendar1_and_calendar_2_array) {
    Prepare();
    DatumArray calendar1_array = DatumArray{
            R"({"weekday_calendar": {)",
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            kNullDatum,
            R"(} })"};
    DatumArray calendar2_array = DatumArray{
            R"({"weekday_calendar": {)",
            kNullDatum,
            R"("friday": {"use_day": true, "shift": {"begin": 0, "end": 1000} })",
            R"(} })"};
    const auto result = RunConstantCalendars(calendar1_array, calendar2_array, 2).value();
    ASSERT_EQ(1, result->size());
    ValidateRow(result, 0, std::nullopt);
}

} // namespace starrocks
