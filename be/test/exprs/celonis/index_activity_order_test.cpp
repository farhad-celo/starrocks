#include "exprs/celonis/index_activity_order.h"

#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisIndexActivityOrderTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
};

TEST_F(CelonisIndexActivityOrderTest, array_celonis_index_activity_nulls) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    // Input data:
    //  row 0 [2]
    //  row 1 [NULL, 10]
    //  row 2 [10, NULL, 10, 20, 10]
    array->append_datum(DatumArray{2});
    array->append_datum(DatumArray{Datum(), 10});
    array->append_datum(DatumArray{Datum(10), Datum(), Datum(10), Datum(20), Datum(10)});

    const auto result = CelonisIndexActivityOrder::celonis_index_activity_order(ctx.get(), {array}).value();
    ASSERT_EQ(3, result->size());
    // First row is [1].
    EXPECT_EQ(1, result->get(0).get_array().size());
    EXPECT_EQ(1, result->get(0).get_array()[0].get_int64());

    // Second row is [NULL, 1].
    EXPECT_EQ(2, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_EQ(1, result->get(1).get_array()[1].get_int64());
    // Third row is [1, NULL, 2, 3, 4]
    EXPECT_EQ(5, result->get(2).get_array().size());
    EXPECT_EQ(1, result->get(2).get_array()[0].get_int64());
    EXPECT_TRUE(result->get(2).get_array()[1].is_null());
    EXPECT_EQ(2, result->get(2).get_array()[2].get_int64());
    EXPECT_EQ(3, result->get(2).get_array()[3].get_int64());
    EXPECT_EQ(4, result->get(2).get_array()[4].get_int64());
}

TEST_F(CelonisIndexActivityOrderTest, array_celonis_index_activity_empty_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    // Input data:
    //  row 0 []
    //  row 1 [NULL]
    //  row 2 NULL
    array->append_datum(DatumArray{});
    array->append_datum(DatumArray{Datum()});
    array->append_datum(Datum());

    const auto result = CelonisIndexActivityOrder::celonis_index_activity_order(ctx.get(), {array}).value();
    ASSERT_EQ(3, result->size());
    // First row is [].
    EXPECT_EQ(0, result->get(0).get_array().size());
    // Second row is [NULL].
    EXPECT_EQ(1, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    // Third row is NULL.
    EXPECT_TRUE(result->get(2).is_null());
}

TEST_F(CelonisIndexActivityOrderTest, array_celonis_index_activity_non_null_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    // Input data:
    //  row 0 [2]
    //  row 2 [10, 20, 10]
    array->append_datum(DatumArray{2});
    array->append_datum(DatumArray{Datum(10), Datum(20), Datum(10)});

    const auto result = CelonisIndexActivityOrder::celonis_index_activity_order(ctx.get(), {array}).value();
    ASSERT_EQ(2, result->size());
    // First row is [1].
    EXPECT_EQ(1, result->get(0).get_array().size());
    EXPECT_EQ(1, result->get(0).get_array()[0].get_int64());
    // Second row is [1, 2, 3].
    EXPECT_EQ(3, result->get(1).get_array().size());
    EXPECT_EQ(1, result->get(1).get_array()[0].get_int64());
    EXPECT_EQ(2, result->get(1).get_array()[1].get_int64());
    EXPECT_EQ(3, result->get(1).get_array()[2].get_int64());
}

} // namespace starrocks
