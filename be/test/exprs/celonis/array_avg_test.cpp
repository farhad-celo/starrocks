#include "exprs/celonis/array_avg.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisArrayAvgTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);

};

TEST_F(CelonisArrayAvgTest, const_null_column) {
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
        arrays->append_datum(kNullDatum);
        const auto result = CelonisArrayAvg<TYPE_INT>::array_avg(nullptr, {ConstColumn::create(arrays, 2)}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto arrays = ColumnHelper::create_const_null_column(2);
        const auto result = CelonisArrayAvg<TYPE_INT>::array_avg(nullptr, {arrays}).value();
        EXPECT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisArrayAvgTest, array_int) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1, kNullDatum, 2, 3});
    arrays->append_datum(DatumArray{kNullDatum, 2, kNullDatum, 3, kNullDatum, 4});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-100});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, 1, 2});
    const auto result = CelonisArrayAvg<TYPE_INT>::array_avg(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2.0, result->get(2).get_double());
    EXPECT_EQ(3.0, result->get(3).get_double());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(-100.0, result->get(5).get_double());
    EXPECT_EQ(1.5, result->get(6).get_double());
}

TEST_F(CelonisArrayAvgTest, array_bigint) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1L, kNullDatum, 2L, 3L});
    arrays->append_datum(DatumArray{kNullDatum, 2L, kNullDatum, 3L, kNullDatum, 4L});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{100L, 100L});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, -7L, 7L, 9L});
    const auto result = CelonisArrayAvg<TYPE_BIGINT>::array_avg(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2.0, result->get(2).get_double());
    EXPECT_EQ(3.0, result->get(3).get_double());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(100.0, result->get(5).get_double());
    EXPECT_EQ(3.0, result->get(6).get_double());
}

TEST_F(CelonisArrayAvgTest, array_double) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1.2, kNullDatum, 2.5, 2.3});
    arrays->append_datum(DatumArray{kNullDatum, -2.3, kNullDatum, 2.3, kNullDatum, 3.0});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-120.3, -120.3});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, 3.14, 3.14, 3.14});
    const auto result = CelonisArrayAvg<TYPE_DOUBLE>::array_avg(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2.0, result->get(2).get_double());
    EXPECT_EQ(1.0, result->get(3).get_double());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(-120.3, result->get(5).get_double());
    EXPECT_EQ(3.14, result->get(6).get_double());
}

TEST_F(CelonisArrayAvgTest, array_double_non_null) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{1.2, 2.5, 2.3});
    arrays->append_datum(DatumArray{-2.3, 2.3, 3.0});
    arrays->append_datum(DatumArray{});
    arrays->append_datum(DatumArray{-120.3, -120.3});
    arrays->append_datum(DatumArray{3.14, 3.14, 3.14});
    const auto result = CelonisArrayAvg<TYPE_DOUBLE>::array_avg(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2.0, result->get(2).get_double());
    EXPECT_EQ(1.0, result->get(3).get_double());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(-120.3, result->get(5).get_double());
    EXPECT_EQ(3.14, result->get(6).get_double());
}

} // namespace starrocks
