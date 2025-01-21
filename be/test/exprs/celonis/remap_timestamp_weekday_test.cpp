#include "exprs/celonis/remap_timestamp_weekday.h"

#include "column/column_helper.h"
#include "exprs/function_context.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisRemapTimestampWeekdayTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(CelonisRemapTimestampWeekdayTest, remap_weekdays_scalar) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
    input->append_datum(Datum(TimestampValue::create(2017, 10, 19, 17, 32, 32)));
    input->append_datum(Datum());
    input->append_datum(Datum(TimestampValue::create(2017, 10, 19, 18, 30, 30)));
    input->append_datum(Datum(TimestampValue::create(1970, 1, 1, 0, 0, 0)));
    const auto result = CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday(ctx.get(), {input}).value();
    ASSERT_EQ(input->size(), result->size());
    EXPECT_EQ(12470L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(12470L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

} // namespace starrocks
