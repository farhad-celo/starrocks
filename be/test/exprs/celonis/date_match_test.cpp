#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/time_functions.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisDateMatchTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(TYPE_DATETIME), TypeDescriptor::from_logical_type(TYPE_ARRAY),
                TypeDescriptor::from_logical_type(TYPE_ARRAY),    TypeDescriptor::from_logical_type(TYPE_ARRAY),
                TypeDescriptor::from_logical_type(TYPE_ARRAY),    TypeDescriptor::from_logical_type(TYPE_ARRAY)};
        auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        timestamp_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        years_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BIGINT), true);
        quarters_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BIGINT), true);
        months_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BIGINT), true);
        weeks_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BIGINT), true);
        days_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_BIGINT), true);
    }

    void AddRow(const Datum& timestamp, const DatumArray& years_array, const DatumArray& quarters_array,
                const DatumArray& months_array, const DatumArray& weeks_array, const DatumArray& days_array) {
        timestamp_column_->append_datum(timestamp);
        years_column_->append_datum(years_array);
        quarters_column_->append_datum(quarters_array);
        months_column_->append_datum(months_array);
        weeks_column_->append_datum(weeks_array);
        days_column_->append_datum(days_array);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local(
                [this] { CelonisTimeFunctions::date_match_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisTimeFunctions::date_match_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local(
                [this] { CelonisTimeFunctions::date_match_close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisTimeFunctions::date_match_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisTimeFunctions::date_match(ctx_.get(), {timestamp_column_, years_column_, quarters_column_,
                                                               months_column_, weeks_column_, days_column_});
        return result;
    }

    StatusOr<ColumnPtr> RunConstantConfig(const DatumArray& years_array, const DatumArray& quarters_array,
                                          const DatumArray& months_array, const DatumArray& weeks_array,
                                          const DatumArray& days_array) {
        years_column_->append_datum(years_array);
        quarters_column_->append_datum(quarters_array);
        months_column_->append_datum(months_array);
        weeks_column_->append_datum(weeks_array);
        days_column_->append_datum(days_array);
        const auto nrows = timestamp_column_->size();
        years_column_ = ConstColumn::create(years_column_, nrows);
        quarters_column_ = ConstColumn::create(quarters_column_, nrows);
        months_column_ = ConstColumn::create(months_column_, nrows);
        weeks_column_ = ConstColumn::create(weeks_column_, nrows);
        days_column_ = ConstColumn::create(days_column_, nrows);
        ctx_->set_constant_columns(
                {nullptr, years_column_, quarters_column_, months_column_, weeks_column_, days_column_});
        return Run();
    }

    StatusOr<ColumnPtr> RunConstantConfig() {
        const auto nrows = timestamp_column_->size();
        ctx_->set_constant_columns(
                {nullptr, ConstColumn::create(years_column_, nrows), ConstColumn::create(quarters_column_, nrows),
                 ConstColumn::create(months_column_, nrows), ConstColumn::create(weeks_column_, nrows),
                 ConstColumn::create(days_column_, nrows)});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr timestamp_column_;
    ColumnPtr years_column_;
    ColumnPtr quarters_column_;
    ColumnPtr months_column_;
    ColumnPtr weeks_column_;
    ColumnPtr days_column_;
};

TEST_F(CelonisDateMatchTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisDateMatchTest, const_config) {
    Prepare();
    timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2008, 2, 8, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2008, 3, 15, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2008, 5, 22, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2007, 1, 1, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2008, 3, 8, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2008, 3, 16, 0, 0, 0));
    timestamp_column_->append_datum(TimestampValue::create(2009, 6, 22, 0, 0, 0));
    const auto result = RunConstantConfig(DatumArray{2008L}, DatumArray{1L, 2L}, DatumArray{1L, 2L, 3L, 5L},
                                          DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L})
                                .value();
    ASSERT_EQ(timestamp_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(0L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisDateMatchTest, non_const_config) {
    {
        Prepare();
        AddRow(TimestampValue::create(2008, 1, 1, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 2, 8, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 3, 15, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 5, 22, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2007, 1, 1, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 3, 8, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 3, 16, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2009, 6, 22, 0, 0, 0), DatumArray{2008L}, DatumArray{1L, 2L},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{1L, 6L, 11L, 21L}, DatumArray{1L, 8L, 15L, 22L});
        const auto result = Run().value();
        ASSERT_EQ(8, result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
        EXPECT_EQ(1L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(0L, result->get(5).get_int64());
        EXPECT_EQ(0L, result->get(6).get_int64());
        EXPECT_EQ(0L, result->get(7).get_int64());
    }
    {
        Prepare();
        AddRow(TimestampValue::create(2007, 1, 1, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{}, DatumArray{},
               DatumArray{1L, 2L, 3L, 4L});
        AddRow(TimestampValue::create(2008, 2, 5, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{}, DatumArray{},
               DatumArray{1L, 2L, 3L, 4L});
        AddRow(TimestampValue::create(2008, 3, 10, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{},
               DatumArray{}, DatumArray{1L, 2L, 3L, 4L});
        AddRow(TimestampValue::create(2009, 5, 15, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{},
               DatumArray{}, DatumArray{1L, 2L, 3L, 4L});
        const auto result = Run().value();
        ASSERT_EQ(4, result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
        EXPECT_EQ(0L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
    }
    {
        Prepare();
        AddRow(TimestampValue::create(2008, 1, 1, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{1L, 2L, 3L, 5L},
               DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 2, 8, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{1L, 2L, 3L, 5L},
               DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 3, 15, 0, 0, 0), DatumArray{2008L}, DatumArray{},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 5, 22, 0, 0, 0), DatumArray{2008L}, DatumArray{},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        const auto result = Run().value();
        ASSERT_EQ(4, result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
        EXPECT_EQ(1L, result->get(3).get_int64());
    }
    {
        Prepare();
        AddRow(TimestampValue::create(2008, 1, 1, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{1L, 2L, 3L, 5L},
               DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 2, 8, 0, 0, 0), DatumArray{2008L}, DatumArray{}, DatumArray{1L, 2L, 3L, 5L},
               DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 3, 15, 0, 0, 0), DatumArray{2008L}, DatumArray{},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2008, 5, 22, 0, 0, 0), DatumArray{2008L}, DatumArray{},
               DatumArray{1L, 2L, 3L, 5L}, DatumArray{}, DatumArray{1L, 8L, 15L, 22L});
        AddRow(TimestampValue::create(2001, 1, 1, 0, 0, 0), DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{1L},
               DatumArray{});
        const auto result = Run().value();
        ASSERT_EQ(5, result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
        EXPECT_EQ(1L, result->get(3).get_int64());
        EXPECT_EQ(1L, result->get(4).get_int64());
    }
}

TEST_F(CelonisDateMatchTest, const_config_null_input) {
    // NULL timestamp
    {
        Prepare();
        timestamp_column_->append_datum(kNullDatum);
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = RunConstantConfig().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL years
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(kNullDatum);
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = RunConstantConfig().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL quarters
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(kNullDatum);
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = RunConstantConfig().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL months
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 2, 8, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(kNullDatum);
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = RunConstantConfig().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL weeks
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(kNullDatum);
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = RunConstantConfig().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL days
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(kNullDatum);
        const auto result = RunConstantConfig().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisDateMatchTest, non_const_config_null_input) {
    // NULL timestamp
    {
        Prepare();
        timestamp_column_->append_datum(kNullDatum);
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL years
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(kNullDatum);
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL quarters
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(kNullDatum);
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL months
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 2, 8, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(kNullDatum);
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL weeks
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(kNullDatum);
        days_column_->append_datum(DatumArray{1L, 8L, 15L, 22L});
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL days
    {
        Prepare();
        timestamp_column_->append_datum(TimestampValue::create(2008, 1, 1, 0, 0, 0));
        years_column_->append_datum(DatumArray{2008L});
        quarters_column_->append_datum(DatumArray{1L, 2L});
        months_column_->append_datum(DatumArray{1L, 2L, 3L, 5L});
        weeks_column_->append_datum(DatumArray{1L, 6L, 11L, 21L});
        days_column_->append_datum(kNullDatum);
        const auto result = Run().value();
        ASSERT_EQ(timestamp_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

} // namespace starrocks
