#include "exprs/celonis/source_target.h"

#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisSourceTargetTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
};

TEST_F(CelonisSourceTargetTest, array_celonis_source) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    // Input data:
    //  row 0 [2]
    //  row 1 [3, 4]
    //  row 2 [14, 15, 16]
    array->append_datum(DatumArray{2});
    array->append_datum(DatumArray{3, 4});
    array->append_datum(DatumArray{14, 15, 16});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");

    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(nullptr, {array, modifier}).value();
    EXPECT_EQ(3, result->size());
    // First row is an empty array.
    EXPECT_EQ(0, result->get(0).get_array().size());
    // Second row is [3].
    EXPECT_EQ(1, result->get(1).get_array().size());
    EXPECT_EQ(3, result->get(1).get_array()[0].get_int32());
    // Third row is [14, 15].
    EXPECT_EQ(2, result->get(2).get_array().size());
    EXPECT_EQ(14, result->get(2).get_array()[0].get_int32());
    EXPECT_EQ(15, result->get(2).get_array()[1].get_int32());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_array_input) {
    // Input data:
    //  row 0 [2]
    //  row 1 []
    //  row 2 [3, 4]
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    array->append_datum(DatumArray{2});
    array->append_datum(DatumArray{});
    array->append_datum(DatumArray{3, 4});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");
    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}).value();
    EXPECT_EQ(3, result->size());
    EXPECT_EQ(0, result->get(0).get_array().size());
    EXPECT_EQ(0, result->get(1).get_array().size());
    EXPECT_EQ(1, result->get(2).get_array().size());
    EXPECT_EQ(3, result->get(2).get_array()[0].get_int32());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_array_input_nullable) {
    // Same as above but the input array is Nullable.
    // Input data:
    //  row 0 [2]
    //  row 1 []
    //  row 2 [3, 4]
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    array->append_datum(DatumArray{2});
    array->append_datum(DatumArray{});
    array->append_datum(DatumArray{3, 4});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");
    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}).value();
    EXPECT_EQ(3, result->size());
    EXPECT_EQ(0, result->get(0).get_array().size());
    EXPECT_EQ(0, result->get(1).get_array().size());
    EXPECT_EQ(1, result->get(2).get_array().size());
    EXPECT_EQ(3, result->get(2).get_array()[0].get_int32());
}


TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_input) {
    // Input is empty.
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");
    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_input_nullable) {
    // Same as above, but array is Nullable.
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");
    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_null_in_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    // Input data:
    //  row 0 [2]
    //  row 1 [NULL, 10]
    //  row 2 [10, NULL, NULL]
    array->append_datum(DatumArray{2});
    array->append_datum(DatumArray{Datum(), 10});
    array->append_datum(DatumArray{Datum(10), Datum(), Datum()});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");
    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}).value();
    EXPECT_EQ(3, result->size());
    // First row is an empty array.
    EXPECT_EQ(0, result->get(0).get_array().size());
    // Second row is [NULL].
    EXPECT_EQ(1, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    // Third row is [10, NULL]
    EXPECT_EQ(2, result->get(2).get_array().size());
    EXPECT_EQ(10, result->get(2).get_array()[0].get_int32());
    EXPECT_TRUE(result->get(2).get_array()[1].is_null());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_unsupported_mode) {
    // "any->all" is not supported.
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    array->append_datum(DatumArray{2});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->all");
    EXPECT_THROW(CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}), std::runtime_error);
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_string_data) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    // Input data:
    //  row 0 ["string1", "string2"]
    //  row 1 [NULL, "string3", NULL, "string4"]
    //  row 2 ["string5"]
    array->append_datum(DatumArray{"string1", "string2"});
    array->append_datum(DatumArray{Datum(), "string3", Datum(), "string4"});
    array->append_datum(DatumArray{"string5"});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");

    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(nullptr, {array, modifier}).value();
    EXPECT_EQ(3, result->size());
    // First row is ["string1"].
    EXPECT_EQ(1, result->get(0).get_array().size());
    EXPECT_EQ("string1", result->get(0).get_array()[0].get_slice());

    // Second row is [NULL, "string4", NULL].
    EXPECT_EQ(3, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_EQ("string3", result->get(1).get_array()[1].get_slice());
    EXPECT_TRUE(result->get(1).get_array()[2].is_null());
    // Third row is [].
    EXPECT_EQ(0, result->get(2).get_array().size());
}

TEST_F(CelonisSourceTargetTest, celonis_source_bigint_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, false);
    array->append_datum(DatumArray{(int64_t) 2000});
    array->append_datum(DatumArray{(int64_t) 200000000, (int64_t) 121, (int64_t) 300});
    array->append_datum(DatumArray{(int64_t) 33, Datum(), (int64_t) 300});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");
    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}).value();
    EXPECT_EQ(3, result->size());
    // First row is an empty array.
    EXPECT_EQ(0, result->get(0).get_array().size());
    // Second row is [200000000, 121].
    EXPECT_EQ(2, result->get(1).get_array().size());
    EXPECT_EQ(200000000, result->get(1).get_array()[0].get_int64());
    EXPECT_EQ(121, result->get(1).get_array()[1].get_int64());
    // Third row is [33, NULL]
    EXPECT_EQ(2, result->get(2).get_array().size());
    EXPECT_EQ(33, result->get(2).get_array()[0].get_int64());
    EXPECT_TRUE(result->get(2).get_array()[1].is_null());
}

TEST_F(CelonisSourceTargetTest, celonis_source_null_in_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    // row 0 : NULL
    // row 1: [NULL, NULL]
    // row 2: []
    array->append_datum(Datum());
    array->append_datum(DatumArray{Datum(), Datum()});
    array->append_datum(DatumArray{});
    auto modifier = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    modifier->append_datum("any->any");
    const auto result = CelonisSourceTargetFunctions::celonis_array_sources(ctx.get(), {array, modifier}).value();
    EXPECT_EQ(3, result->size());
    // Result:
    // row 0: NULL
    // row 1: [NULL]
    // row 2: []
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(1, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_FALSE(result->get(2).is_null());
    EXPECT_EQ(0, result->get(2).get_array().size());
}

} // namespace starrocks
