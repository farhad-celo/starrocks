#include "exprs/celonis/array_end_finder.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

namespace starrocks {

class CelonisArrayEndFinderTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);
};

TEST_F(CelonisArrayEndFinderTest, celonis_array_first_const_null) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    arrays->append_datum(kNullDatum);
    const auto result = CelonisArrayEndFinder<TYPE_INT>::array_first(nullptr, {ConstColumn::create(arrays, 2)}).value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_first_int) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1, kNullDatum, 2});
    arrays->append_datum(DatumArray{kNullDatum, 2, kNullDatum, 3, kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-100});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, 7});
    const auto result = CelonisArrayEndFinder<TYPE_INT>::array_first(nullptr, {arrays}).value();
    ASSERT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(1, result->get(2).get_int32());
    EXPECT_EQ(2, result->get(3).get_int32());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(-100, result->get(5).get_int32());
    EXPECT_EQ(7, result->get(6).get_int32());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_first_bigint) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1L, kNullDatum, 2L});
    arrays->append_datum(DatumArray{kNullDatum, 2L, kNullDatum, 3L, kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{100L});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, -7L});
    const auto result = CelonisArrayEndFinder<TYPE_BIGINT>::array_first(nullptr, {arrays}).value();
    ASSERT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(2L, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(100L, result->get(5).get_int64());
    EXPECT_EQ(-7L, result->get(6).get_int64());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_first_double) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1.2, kNullDatum, 2.5});
    arrays->append_datum(DatumArray{kNullDatum, -2.3, kNullDatum, 3.0, kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-120.3});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, 3.14});
    const auto result = CelonisArrayEndFinder<TYPE_DOUBLE>::array_first(nullptr, {arrays}).value();
    ASSERT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(1.2, result->get(2).get_double());
    EXPECT_EQ(-2.3, result->get(3).get_double());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(-120.3, result->get(5).get_double());
    EXPECT_EQ(3.14, result->get(6).get_double());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_first_varchar) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, "a", kNullDatum, "B"});
    arrays->append_datum(DatumArray{kNullDatum, "", kNullDatum, "C", kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{"APPLE"});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, "Google"});
    const auto result = CelonisArrayEndFinder<TYPE_VARCHAR>::array_first(nullptr, {arrays}).value();
    ASSERT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ("a", result->get(2).get_slice());
    EXPECT_EQ("", result->get(3).get_slice());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ("APPLE", result->get(5).get_slice());
    EXPECT_EQ("Google", result->get(6).get_slice());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_first_datetime) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, TimestampValue::create(1970, 1, 1, 0, 0, 0), kNullDatum,
                                    TimestampValue::create(1970, 1, 2, 0, 0, 0)});
    arrays->append_datum(DatumArray{kNullDatum, TimestampValue::create(1972, 1, 1, 0, 0, 0), kNullDatum,
                                    TimestampValue::create(1973, 1, 1, 0, 0, 0), kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{TimestampValue::create(1950, 1, 1, 0, 0, 0)});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, TimestampValue::create(2023, 7, 10, 1, 2, 3)});
    const auto result = CelonisArrayEndFinder<TYPE_DATETIME>::array_first(nullptr, {arrays}).value();
    ASSERT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(TimestampValue::create(1970, 1, 1, 0, 0, 0), result->get(2).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1972, 1, 1, 0, 0, 0), result->get(3).get_timestamp());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(TimestampValue::create(1950, 1, 1, 0, 0, 0), result->get(5).get_timestamp());
    EXPECT_EQ(TimestampValue::create(2023, 7, 10, 1, 2, 3), result->get(6).get_timestamp());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_last_const_null) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    arrays->append_datum(kNullDatum);
    const auto result = CelonisArrayEndFinder<TYPE_INT>::array_last(nullptr, {ConstColumn::create(arrays, 2)}).value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_last_int) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1, kNullDatum, 2});
    arrays->append_datum(DatumArray{});
    arrays->append_datum(DatumArray{kNullDatum, 2, kNullDatum, 3, kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-100});
    arrays->append_datum(DatumArray{8, kNullDatum, kNullDatum, 7});
    const auto result = CelonisArrayEndFinder<TYPE_INT>::array_last(nullptr, {arrays}).value();
    ASSERT_EQ(8, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2, result->get(2).get_int32());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(3, result->get(4).get_int32());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_EQ(-100, result->get(6).get_int32());
    EXPECT_EQ(7, result->get(7).get_int32());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_last_bigint) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1L, kNullDatum, 2L});
    arrays->append_datum(DatumArray{});
    arrays->append_datum(DatumArray{kNullDatum, 2L, kNullDatum, 3L, kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-100L});
    arrays->append_datum(DatumArray{8L, kNullDatum, kNullDatum, 7L});
    const auto result = CelonisArrayEndFinder<TYPE_BIGINT>::array_last(nullptr, {arrays}).value();
    ASSERT_EQ(8, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(3L, result->get(4).get_int64());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_EQ(-100L, result->get(6).get_int64());
    EXPECT_EQ(7L, result->get(7).get_int64());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_last_double) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1.5, kNullDatum, 2.1});
    arrays->append_datum(DatumArray{});
    arrays->append_datum(DatumArray{kNullDatum, 2.3, kNullDatum, 3.14, kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-123.1});
    arrays->append_datum(DatumArray{8.0, kNullDatum, kNullDatum, 7.7});
    const auto result = CelonisArrayEndFinder<TYPE_DOUBLE>::array_last(nullptr, {arrays}).value();
    ASSERT_EQ(8, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2.1, result->get(2).get_double());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(3.14, result->get(4).get_double());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_EQ(-123.1, result->get(6).get_double());
    EXPECT_EQ(7.7, result->get(7).get_double());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_last_varchar) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, "a", kNullDatum, "B"});
    arrays->append_datum(DatumArray{kNullDatum, "C", kNullDatum, "", kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{"apple", "APPLE"});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, "Google"});
    arrays->append_datum(DatumArray{"apple"});
    const auto result = CelonisArrayEndFinder<TYPE_VARCHAR>::array_last(nullptr, {arrays}).value();
    ASSERT_EQ(arrays->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ("B", result->get(3).get_slice());
    EXPECT_EQ("", result->get(4).get_slice());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_EQ("APPLE", result->get(6).get_slice());
    EXPECT_EQ("Google", result->get(7).get_slice());
    EXPECT_EQ("apple", result->get(8).get_slice());
}

TEST_F(CelonisArrayEndFinderTest, celonis_array_last_datetime) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, TimestampValue::create(1970, 1, 1, 0, 0, 0), kNullDatum,
                                    TimestampValue::create(1970, 1, 2, 0, 0, 0)});
    arrays->append_datum(DatumArray{kNullDatum, TimestampValue::create(1972, 1, 1, 0, 0, 0), kNullDatum,
                                    TimestampValue::create(1973, 1, 1, 0, 0, 0), kNullDatum});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{TimestampValue::create(1950, 1, 1, 0, 0, 0)});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, TimestampValue::create(2023, 7, 10, 1, 2, 3)});
    const auto result = CelonisArrayEndFinder<TYPE_DATETIME>::array_last(nullptr, {arrays}).value();
    ASSERT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(TimestampValue::create(1970, 1, 2, 0, 0, 0), result->get(2).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1973, 1, 1, 0, 0, 0), result->get(3).get_timestamp());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(TimestampValue::create(1950, 1, 1, 0, 0, 0), result->get(5).get_timestamp());
    EXPECT_EQ(TimestampValue::create(2023, 7, 10, 1, 2, 3), result->get(6).get_timestamp());
}

} // namespace starrocks
