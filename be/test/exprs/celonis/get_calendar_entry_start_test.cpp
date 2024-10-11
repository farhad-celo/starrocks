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

class CelonisGetCalendarEntryStartTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        index_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
        calendar_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_VARCHAR), true);
        calendar_id_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisTimeFunctions::get_calendar_entry_start_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::get_calendar_entry_start_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisTimeFunctions::get_calendar_entry_start_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisTimeFunctions::get_calendar_entry_start_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisTimeFunctions::get_calendar_entry_start(ctx_.get(),
                                                                {index_column_, calendar_column_, calendar_id_column_});
        return result;
    }

    StatusOr<ColumnPtr>
    RunConstantCalendar(const std::vector<std::string>& calendar_strs) {
        DatumArray calendar_array;
        for (const auto& calendar_str: calendar_strs) {
            calendar_array.emplace_back(calendar_str.c_str());
        }
        calendar_column_->append_datum(calendar_array);
        calendar_column_ = ConstColumn::create(calendar_column_, index_column_->size());
        ctx_->set_constant_columns({nullptr, calendar_column_, nullptr});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr index_column_;
    ColumnPtr calendar_column_;
    ColumnPtr calendar_id_column_;
};

TEST_F(CelonisGetCalendarEntryStartTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisGetCalendarEntryStartTest, normal_cases_without_calendar_id) {
    {
        Prepare();
        index_column_->append_datum(0);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(28800000, result->get(0).get_int64());
    }
    {
        Prepare();
        index_column_->append_datum(-1);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        index_column_->append_datum(0);
        index_column_->append_datum(1);
        index_column_->append_datum(2);
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        // the first two entries are merged, so there are two entries
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 1, "end_date": 11}, )",
                 R"("entries": {"start_date": 7, "end_date": 21}, )",
                 R"("entries": {"start_date": 500, "end_date": 600}, )",
                 R"( }})"}).value();
        ASSERT_EQ(3, result->size());
        EXPECT_EQ(1, result->get(0).get_int64());
        EXPECT_EQ(500, result->get(1).get_int64());
        EXPECT_TRUE(result->get(2).is_null());
    }
    {
        Prepare();
        index_column_->append_datum(0);
        index_column_->append_datum(1);
        calendar_id_column_->append_datum(kNullDatum);
        calendar_id_column_->append_datum(kNullDatum);
        // The entries are sorted
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 700, "end_date": 800}, )",
                 R"("entries": {"start_date": 500, "end_date": 600}, )",
                 R"( }})"}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(500, result->get(0).get_int64());
        EXPECT_EQ(700, result->get(1).get_int64());
    }
}

TEST_F(CelonisGetCalendarEntryStartTest, normal_cases_with_calendar_id) {
    {
        Prepare();
        index_column_->append_datum(0);
        calendar_id_column_->append_datum(kNullDatum);
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "US"} }})"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        index_column_->append_datum(0);
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "US"} }})"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(28800000, result->get(0).get_int64());
    }
    {
        Prepare();
        index_column_->append_datum(-1);
        calendar_id_column_->append_datum("US");
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000, "calendar_id": "US"} }})"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        index_column_->append_datum(0);
        index_column_->append_datum(0);
        index_column_->append_datum(1);
        calendar_id_column_->append_datum("US");
        calendar_id_column_->append_datum("UK");
        calendar_id_column_->append_datum("US");
        // the first two entries are merged
        const auto result = RunConstantCalendar(
                {R"({"factory_calendar": {)",
                 R"("entries": {"start_date": 1, "end_date": 11, "calendar_id": "US"}, )",
                 R"("entries": {"start_date": 7, "end_date": 21, "calendar_id": "UK"}, )",
                 R"("entries": {"start_date": 500, "end_date": 600, "calendar_id": "US"}, )",
                 R"( }})"}).value();
        ASSERT_EQ(3, result->size());
        EXPECT_EQ(1, result->get(0).get_int64());
        EXPECT_EQ(7, result->get(1).get_int64());
        EXPECT_EQ(500, result->get(2).get_int64());
    }
}

TEST_F(CelonisGetCalendarEntryStartTest, non_const_calendar) {
    Prepare();
    index_column_->append_datum(0);
    index_column_->append_datum(0);
    calendar_column_->append_datum(
            DatumArray{R"({"factory_calendar": { "entries": {"start_date": 28800000, "end_date": 61200000} }})"});
    calendar_column_->append_datum(
            DatumArray{R"({"factory_calendar": { "entries": {"start_date": 1, "end_date": 2} }})"});
    calendar_id_column_->append_datum("US");
    calendar_id_column_->append_datum("UK");
    const auto result = Run();
    ASSERT_TRUE(result.status().is_not_supported());
    EXPECT_EQ(result.status().message(), "Non-const calendar is not supported in get_calendar_entry_start.");
}

} // namespace starrocks
