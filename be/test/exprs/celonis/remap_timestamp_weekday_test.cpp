#include "exprs/celonis/remap_timestamp_weekday.h"

#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisRemapTimestampWeekdayTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);
};

TEST_F(CelonisRemapTimestampWeekdayTest, remap_weekdays) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, false);
    array->append_datum(DatumArray{TimestampValue::create(2017, 10, 19, 17, 32, 32)});
    array->append_datum(DatumArray{TimestampValue::create(2017, 10, 20, 2, 32, 32),
                                   TimestampValue::create(2017, 10, 20, 2, 32, 55),
                                   TimestampValue::create(2017, 10, 29, 21, 20, 50)});

    const auto result = CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday(ctx.get(), {array}).value();
    ASSERT_EQ(2, result->size());
    EXPECT_EQ(1, result->get(0).get_array().size());
    EXPECT_EQ(12470L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(3, result->get(1).get_array().size());
    EXPECT_EQ(12471L, result->get(1).get_array()[0].get_int64());
    EXPECT_EQ(12471L, result->get(1).get_array()[1].get_int64());
    EXPECT_EQ(12477L, result->get(1).get_array()[2].get_int64());
}

TEST_F(CelonisRemapTimestampWeekdayTest, remap_weekdays_with_nulls) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());

    auto array = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
    // Input data:
    // row 0: ["2017-10-19 17:32:32", NULL, "2017-10-19 18:30:30"]
    // row 1: NULL
    // row 2: []
    array->append_datum(DatumArray{TimestampValue::create(2017, 10, 19, 17, 32, 32), Datum(),
                                   TimestampValue::create(2017, 10, 19, 18, 30, 30)});
    array->append_datum(Datum());
    array->append_datum(DatumArray());

    const auto result = CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday(ctx.get(), {array}).value();
    ASSERT_EQ(3, result->size());
    EXPECT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(12470L, result->get(0).get_array()[0].get_int64());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_EQ(12470L, result->get(0).get_array()[2].get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(0, result->get(2).get_array().size());
}

TEST_F(CelonisRemapTimestampWeekdayTest, remap_weekdays_scalar) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
    input->append_datum(Datum(TimestampValue::create(2017, 10, 19, 17, 32, 32)));
    input->append_datum(Datum());
    input->append_datum(Datum(TimestampValue::create(2017, 10, 19, 18, 30, 30)));
    const auto result = CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday_scalar(ctx.get(), {input}).value();
    ASSERT_EQ(3, result->size());
    EXPECT_EQ(12470L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(12470L, result->get(2).get_int64());
}
} // namespace starrocks
