#include "exprs/celonis/array_count_distinct.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "util.h"

namespace starrocks {

class CelonisArrayCountDistinctTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);
};

TEST_F(CelonisArrayCountDistinctTest, const_null_column) {
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
        arrays->append_datum(kNullDatum);
        const auto result =
                CelonisArrayCountDistinct<TYPE_INT>::array_count_distinct(nullptr, {ConstColumn::create(arrays, 2)})
                        .value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto arrays = ColumnHelper::create_const_null_column(2);
        const auto result = CelonisArrayCountDistinct<TYPE_INT>::array_count_distinct(nullptr, {arrays}).value();
        EXPECT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisArrayCountDistinctTest, array_int) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1, kNullDatum, 2, 1});
    arrays->append_datum(DatumArray{kNullDatum, 2, kNullDatum, 3, kNullDatum, 3});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-100});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, 7, 7, 7});
    const auto result = CelonisArrayCountDistinct<TYPE_INT>::array_count_distinct(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_EQ(2L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
}

TEST_F(CelonisArrayCountDistinctTest, array_bigint) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1L, kNullDatum, 2L, 1L});
    arrays->append_datum(DatumArray{kNullDatum, 2L, kNullDatum, 3L, kNullDatum, 2L});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{100L, 100L});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, -7L, 7L, 7L});
    const auto result = CelonisArrayCountDistinct<TYPE_BIGINT>::array_count_distinct(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_EQ(2L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(2L, result->get(6).get_int64());
}

TEST_F(CelonisArrayCountDistinctTest, array_double) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1.2, kNullDatum, 2.5, 1.2});
    arrays->append_datum(DatumArray{kNullDatum, -2.3, kNullDatum, 3.0, kNullDatum, 3.0});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-120.3, -120.3});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, 3.14, 3.14, 5.3});
    const auto result = CelonisArrayCountDistinct<TYPE_DOUBLE>::array_count_distinct(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_EQ(2L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(2L, result->get(6).get_int64());
}

TEST_F(CelonisArrayCountDistinctTest, array_varchar) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, "a", kNullDatum, "B", "a"});
    arrays->append_datum(DatumArray{kNullDatum, "", kNullDatum, "C", kNullDatum, ""});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{"APPLE"});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, "Google", "Facebook"});
    const auto result = CelonisArrayCountDistinct<TYPE_VARCHAR>::array_count_distinct(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_EQ(2L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(2L, result->get(6).get_int64());
}

TEST_F(CelonisArrayCountDistinctTest, array_datetime) {
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
    const auto result = CelonisArrayCountDistinct<TYPE_DATETIME>::array_count_distinct(nullptr, {arrays}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_EQ(2L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
}

} // namespace starrocks
