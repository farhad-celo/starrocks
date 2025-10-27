#include "exprs/celonis/to_double.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"

namespace starrocks {

class CelonisToDoubleTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(CelonisToDoubleTest, const_null_column) {
    {
        auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
        values->append_datum(kNullDatum);
        const auto result = CelonisToDouble<TYPE_INT>::to_double(nullptr, {ConstColumn::create(values, 2)}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto values = ColumnHelper::create_const_null_column(2);
        const auto result = CelonisToDouble<TYPE_INT>::to_double(nullptr, {values}).value();
        EXPECT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisToDoubleTest, big_int_column) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    values->append_datum(kNullDatum);
    values->append_datum(1L);
    values->append_datum(2L);
    values->append_datum(0L);
    values->append_datum(-1L);
    const auto result = CelonisToDouble<TYPE_BIGINT>::to_double(nullptr, {values}).value();
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(1.0, result->get(1).get_double());
    EXPECT_EQ(2.0, result->get(2).get_double());
    EXPECT_EQ(0.0, result->get(3).get_double());
    EXPECT_EQ(-1.0, result->get(4).get_double());
}

TEST_F(CelonisToDoubleTest, int_column) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
    values->append_datum(kNullDatum);
    values->append_datum(1);
    values->append_datum(2);
    values->append_datum(0);
    values->append_datum(-1);
    const auto result = CelonisToDouble<TYPE_INT>::to_double(nullptr, {values}).value();
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(1.0, result->get(1).get_double());
    EXPECT_EQ(2.0, result->get(2).get_double());
    EXPECT_EQ(0.0, result->get(3).get_double());
    EXPECT_EQ(-1.0, result->get(4).get_double());
}

TEST_F(CelonisToDoubleTest, double_column) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);
    values->append_datum(kNullDatum);
    values->append_datum(1.5);
    values->append_datum(2.5);
    values->append_datum(0.0);
    values->append_datum(-1.5);
    const auto result = CelonisToDouble<TYPE_DOUBLE>::to_double(nullptr, {values}).value();
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(1.5, result->get(1).get_double());
    EXPECT_EQ(2.5, result->get(2).get_double());
    EXPECT_EQ(0.0, result->get(3).get_double());
    EXPECT_EQ(-1.5, result->get(4).get_double());
}

TEST_F(CelonisToDoubleTest, datetime_column) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
    values->append_datum(kNullDatum);
    values->append_datum(TimestampValue::create(2022, 1, 1, 0, 0, 0));
    values->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    const auto result = CelonisToDouble<TYPE_DATETIME>::to_double(nullptr, {values}).value();
    EXPECT_EQ(values->size(), result->size());
    EXPECT_EQ(1640995200000.0, result->get(1).get_double());
    EXPECT_EQ(0.0, result->get(2).get_double());
}

} // namespace starrocks
