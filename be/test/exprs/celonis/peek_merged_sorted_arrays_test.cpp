#include "exprs/celonis/peek_merged_sorted_arrays.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

#include <gtest/gtest.h>

namespace starrocks {

class CelonisPeekMergedSortedArraysTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    template<LogicalType InputLT, LogicalType SecondaryLT>
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(InputLT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        input_column_ = ColumnHelper::create_column(celonis::array_type(InputLT), false);
        timestamp_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_DATETIME), false);
        size_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_INT), false);
        priority_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_INT), false);
        secondary_order_column_ = ColumnHelper::create_column(celonis::array_type(SecondaryLT), false);
    }

    void
    AddRow(const DatumArray& input_array, const DatumArray& timestamp_array, const DatumArray& size_array,
           const DatumArray& priority_array, const DatumArray& secondary_order_array) {
        input_column_->append_datum(input_array);
        timestamp_column_->append_datum(timestamp_array);
        size_column_->append_datum(size_array);
        priority_column_->append_datum(priority_array);
        secondary_order_column_->append_datum(secondary_order_array);
    }

    template<LogicalType InputLT>
    StatusOr<ColumnPtr> Run() {
        StatusOr<ColumnPtr> result = CelonisPeekMergedSortedArrays<InputLT>::peek_merged_sorted_arrays(ctx_.get(),
                                                                                                       {input_column_,
                                                                                                        timestamp_column_,
                                                                                                        size_column_,
                                                                                                        priority_column_,
                                                                                                        secondary_order_column_});
        return result;
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr input_column_;
    ColumnPtr timestamp_column_;
    ColumnPtr size_column_;
    ColumnPtr priority_column_;
    ColumnPtr secondary_order_column_;
};

TEST_F(CelonisPeekMergedSortedArraysTest, empty_input) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_BIGINT;
    Prepare<InputLT, SecondaryLT>();
    const auto rs = Run<InputLT>();
    ASSERT_TRUE(rs.ok()) << rs.status().message();
    const auto& result = rs.value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisPeekMergedSortedArraysTest, datetime_input_and_bigint_secondary_order) {
    const LogicalType InputLT = TYPE_DATETIME;
    const LogicalType SecondaryLT = TYPE_BIGINT;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{TimestampValue::create(2110, 1, 1, 0, 0, 0), TimestampValue::create(2120, 1, 1, 0, 0, 0),
                      TimestampValue::create(2215, 1, 1, 0, 0, 0), TimestampValue::create(2225, 1, 1, 0, 0, 0),
                      TimestampValue::create(2310, 1, 1, 0, 0, 0), TimestampValue::create(2315, 1, 1, 0, 0, 0),
                      TimestampValue::create(2320, 1, 1, 0, 0, 0)}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 25),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{1L, 1L, 3L, 3L, 2L, 2L, 2L});
    AddRow(DatumArray{TimestampValue::create(1010, 1, 1, 0, 0, 0), TimestampValue::create(1020, 1, 1, 0, 0, 0),
                      TimestampValue::create(2010, 1, 1, 0, 0, 0), TimestampValue::create(2120, 1, 1, 0, 0, 0)},
           DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2}, DatumArray{1, 1},
           DatumArray{2L, 2L, 1L, 1L});
    const auto rs = Run<InputLT>();
    ASSERT_TRUE(rs.ok()) << rs.status().message();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    EXPECT_EQ(TimestampValue::create(2110, 1, 1, 0, 0, 0), result->get(0).get_timestamp());
    EXPECT_EQ(TimestampValue::create(2010, 1, 1, 0, 0, 0), result->get(1).get_timestamp());
}

TEST_F(CelonisPeekMergedSortedArraysTest, int_input_and_bigint_secondary_order) {
    const LogicalType InputLT = TYPE_INT;
    const LogicalType SecondaryLT = TYPE_BIGINT;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{110, 120, 215, 225, 310, 315, 320}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 25),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{1L, 1L, 3L, 3L, 2L, 2L, 2L});
    AddRow(DatumArray{1010, 1020, 2010, 2020}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2}, DatumArray{1, 1},
           DatumArray{2L, 2L, 1L, 1L});
    const auto rs = Run<InputLT>();
    ASSERT_TRUE(rs.ok()) << rs.status().message();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    EXPECT_EQ(110, result->get(0).get_int32());
    EXPECT_EQ(2010, result->get(1).get_int32());
}

TEST_F(CelonisPeekMergedSortedArraysTest, double_input_and_varchar_secondary_order) {
    const LogicalType InputLT = TYPE_DOUBLE;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{110.5, 120.5, 215.5, 225.5, 310.5, 315.5, 320.5}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 25),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{"apple", "apple", "cat", "cat", "bus", "bus", "bus"});
    AddRow(DatumArray{1010.5, 1020.5, kNullDatum, 2020.5}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2}, DatumArray{1, 1},
           DatumArray{"bus", "bus", "apple", "apple"});
    const auto rs = Run<InputLT>();
    ASSERT_TRUE(rs.ok()) << rs.status().message();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    EXPECT_EQ(110.5, result->get(0).get_double());
    EXPECT_EQ(1010.5, result->get(1).get_double());
}

TEST_F(CelonisPeekMergedSortedArraysTest, varchar_input_and_varchar_secondary_order) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{kNullDatum, "b", "c", "d", "e", "f", "g"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 25),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    AddRow(DatumArray{"apple", "bus", "cat", "dog"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2}, DatumArray{1, 1},
           DatumArray{"b", "b", "a", "a"});
    AddRow(DatumArray{kNullDatum, kNullDatum, kNullDatum, "d", kNullDatum, "foo", "g"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 25),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    AddRow(DatumArray{kNullDatum, kNullDatum, kNullDatum, kNullDatum, kNullDatum, kNullDatum, kNullDatum}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 25),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    const auto rs = Run<InputLT>();
    ASSERT_TRUE(rs.ok()) << rs.status().message();
    const auto& result = rs.value();
    ASSERT_EQ(4, result->size());
    EXPECT_EQ("e", result->get(0).get_slice().to_string());
    EXPECT_EQ("cat", result->get(1).get_slice().to_string());
    EXPECT_EQ("foo", result->get(2).get_slice().to_string());
    EXPECT_TRUE(result->get(3).is_null());
}

TEST_F(CelonisPeekMergedSortedArraysTest, empty_input_arrays) {
    const LogicalType InputLT = TYPE_INT;
    const LogicalType SecondaryLT = TYPE_BIGINT;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{}, DatumArray{}, DatumArray{0, 0, 0}, DatumArray{1, 1, 1},
           DatumArray{});
    const auto rs = Run<InputLT>();
    ASSERT_TRUE(rs.ok()) << rs.status().message();
    const auto& result = rs.value();
    ASSERT_EQ(1, result->size());
    EXPECT_TRUE(result->get(0).is_null());
}

TEST_F(CelonisPeekMergedSortedArraysTest, null_timestamp) {
    {
        const LogicalType InputLT = TYPE_VARCHAR;
        const LogicalType SecondaryLT = TYPE_VARCHAR;
        Prepare<InputLT, SecondaryLT>();
        AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g"}, DatumArray{
                       TimestampValue::create(2023, 1, 1, 0, 0, 10),
                       TimestampValue::create(2023, 1, 1, 0, 0, 20),
                       kNullDatum,
                       TimestampValue::create(2023, 1, 1, 0, 0, 15),
                       TimestampValue::create(2023, 1, 1, 0, 0, 10),
                       TimestampValue::create(2023, 1, 1, 0, 0, 15),
                       TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
               DatumArray{"a", "a", "c", "c", "b", "b", "b"});
        const auto rs = Run<InputLT>();
        ASSERT_TRUE(rs.ok()) << rs.status().message();
        const auto& result = rs.value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("c", result->get(0).get_slice().to_string());
    }
    {
        const LogicalType InputLT = TYPE_VARCHAR;
        const LogicalType SecondaryLT = TYPE_VARCHAR;
        Prepare<InputLT, SecondaryLT>();
        AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g"}, DatumArray{
                       kNullDatum,
                       TimestampValue::create(2023, 1, 1, 0, 0, 20),
                       kNullDatum,
                       TimestampValue::create(2023, 1, 1, 0, 0, 15),
                       kNullDatum,
                       TimestampValue::create(2023, 1, 1, 0, 0, 15),
                       TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
               DatumArray{"a", "a", "c", "c", "b", "b", "b"});
        const auto rs = Run<InputLT>();
        ASSERT_TRUE(rs.ok()) << rs.status().message();
        const auto& result = rs.value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("a", result->get(0).get_slice().to_string());
    }
}

TEST_F(CelonisPeekMergedSortedArraysTest, null_secondary_order) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{"a", "a", "c", "c", kNullDatum, "e", "b"});
    const auto rs = Run<InputLT>();
    ASSERT_TRUE(rs.ok()) << rs.status().message();
    const auto& result = rs.value();
    ASSERT_EQ(1, result->size());
    EXPECT_EQ("e", result->get(0).get_slice().to_string());
}

TEST_F(CelonisPeekMergedSortedArraysTest, null_size) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, kNullDatum, 3}, DatumArray{1, 1, 1},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    const auto result = Run<InputLT>();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "size_array should not have NULL elements.");
}

TEST_F(CelonisPeekMergedSortedArraysTest, null_priority) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, kNullDatum},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    const auto result = Run<InputLT>();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "priority_array should not have NULL elements.");
}

TEST_F(CelonisPeekMergedSortedArraysTest, input_array_size_different_from_timestamp_size) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g", "b"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    const auto result = Run<InputLT>();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "The size of input_array and timestamp_array should not be different.");
}

TEST_F(CelonisPeekMergedSortedArraysTest, size_array_size_different_from_priority_size) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 3}, DatumArray{1, 1, 1, 1},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    const auto result = Run<InputLT>();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "The size of size_array and priority_array should not be different.");
}

TEST_F(CelonisPeekMergedSortedArraysTest, wrong_size_array) {
    const LogicalType InputLT = TYPE_VARCHAR;
    const LogicalType SecondaryLT = TYPE_VARCHAR;
    Prepare<InputLT, SecondaryLT>();
    AddRow(DatumArray{"a", "b", "c", "d", "e", "f", "g"}, DatumArray{
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20),
                   TimestampValue::create(2023, 1, 1, 0, 0, 10),
                   TimestampValue::create(2023, 1, 1, 0, 0, 15),
                   TimestampValue::create(2023, 1, 1, 0, 0, 20)}, DatumArray{2, 2, 4}, DatumArray{1, 1, 1},
           DatumArray{"a", "a", "c", "c", "b", "b", "b"});
    const auto result = Run<InputLT>();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(),
              "The size of input_array and timestamp_array should not be different than the sum of size_array.");
}

} // namespace starrocks
