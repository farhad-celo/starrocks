#include "exprs/celonis/calculate_range_end.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

namespace starrocks {

class CelonisCalculateRangeEndTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        start_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        step_size_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        step_count_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    }

    StatusOr<ColumnPtr> Run() {
        StatusOr<ColumnPtr> result;
        result = CelonisCalculateRangeEnd::calculate_range_end(ctx_.get(),
                                                               {start_column_, step_size_column_, step_count_column_});
        return result;
    }

    StatusOr<ColumnPtr>
    RunConstantConfig(const Datum& step_size, const Datum& step_count) {
        step_size_column_->append_datum(step_size);
        step_count_column_->append_datum(step_count);
        ctx_->set_constant_columns(
                {nullptr, ConstColumn::create(step_size_column_, start_column_->size()),
                 ConstColumn::create(step_count_column_, start_column_->size())});
        StatusOr<ColumnPtr> result;
        result = CelonisCalculateRangeEnd::calculate_range_end(
                ctx_.get(), {start_column_, ConstColumn::create(step_size_column_, start_column_->size()),
                             ConstColumn::create(step_count_column_, start_column_->size())});
        return result;
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr start_column_;
    ColumnPtr step_size_column_;
    ColumnPtr step_count_column_;
};

TEST_F(CelonisCalculateRangeEndTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisCalculateRangeEndTest, null_input) {
    // NULL start
    {
        Prepare();
        start_column_->append_datum(kNullDatum);
        step_size_column_->append_datum("1h");
        step_count_column_->append_datum(10L);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL step_size
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        step_size_column_->append_datum(kNullDatum);
        step_count_column_->append_datum(10L);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    // NULL step_count
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        step_size_column_->append_datum("1h");
        step_count_column_->append_datum(kNullDatum);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisCalculateRangeEndTest, unsupported_time_unit) {
    Prepare();
    start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
    step_size_column_->append_datum("1s");
    step_count_column_->append_datum(10L);
    const auto result = Run();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "CELONIS_CALCULATE_RANGE_END: Invalid step size 1s.");
}

TEST_F(CelonisCalculateRangeEndTest, malformed_time_unit) {
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        step_size_column_->append_datum("1ah");
        step_count_column_->append_datum(10L);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "CELONIS_CALCULATE_RANGE_END: Invalid step size 1ah.");
    }
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        step_size_column_->append_datum("xxx");
        step_count_column_->append_datum(10L);
        const auto result = Run();
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "CELONIS_CALCULATE_RANGE_END: Invalid step size xxx.");
    }
}

TEST_F(CelonisCalculateRangeEndTest, add_hours) {
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("3h"), Datum(10L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 3, 6, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 3, 16, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("-1h"), Datum(10L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 1, 14, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 2, 0, 0, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisCalculateRangeEndTest, add_days) {
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("3D"), Datum(2L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 8, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 8, 10, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 10, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 1, 10, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("-2D"), Datum(3L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 1, 4, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 1, 4, 10, 0, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisCalculateRangeEndTest, add_months) {
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("3M"), Datum(2L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 7, 2, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 7, 2, 10, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 10, 10, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 10, 10, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("-2M"), Datum(3L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2018, 4, 10, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2018, 4, 10, 10, 0, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisCalculateRangeEndTest, add_years) {
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 1, 2, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("3Y"), Datum(2L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(2024, 1, 2, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(2024, 1, 2, 10, 0, 0), result->get(1).get_timestamp());
    }
    {
        Prepare();
        start_column_->append_datum(TimestampValue::create(2018, 10, 10, 0, 0, 0));
        start_column_->append_datum(TimestampValue::create(2018, 10, 10, 10, 0, 0));
        const auto result = RunConstantConfig(Datum("-500Y"), Datum(2L)).value();
        ASSERT_EQ(start_column_->size(), result->size());
        EXPECT_EQ(TimestampValue::create(1018, 10, 10, 0, 0, 0), result->get(0).get_timestamp());
        EXPECT_EQ(TimestampValue::create(1018, 10, 10, 10, 0, 0), result->get(1).get_timestamp());
    }
}

TEST_F(CelonisCalculateRangeEndTest, large_number_does_not_overflow) {
    Prepare();
    start_column_->append_datum(TimestampValue::create(2018, 1, 2, 0, 0, 0));
    step_size_column_->append_datum("1000000000000h");
    step_count_column_->append_datum(1000000000000L);
    const auto result = Run().value();
    ASSERT_EQ(1, result->size());
    EXPECT_TRUE(result->get(0).is_null());
}

} // namespace starrocks
