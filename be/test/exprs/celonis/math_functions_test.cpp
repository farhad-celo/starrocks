#include "exprs/celonis/math_functions.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisMathFunctionsTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(CelonisMathFunctionsTest, square_empty_input) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
    const auto result = CelonisMathFunctions<TYPE_INT>::square(nullptr, {values}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisMathFunctionsTest, square_int_input) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
    values->append_datum(kNullDatum);
    values->append_datum(0);
    values->append_datum(1);
    values->append_datum(4);
    values->append_datum(-5);
    values->append_datum(4500000); // overflow
    values->append_datum(kNullDatum);
    const auto result = CelonisMathFunctions<TYPE_INT>::square(nullptr, {values}).value();
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(0, result->get(1).get_int32());
    EXPECT_EQ(1, result->get(2).get_int32());
    EXPECT_EQ(16, result->get(3).get_int32());
    EXPECT_EQ(25, result->get(4).get_int32());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_TRUE(result->get(6).is_null());
}

TEST_F(CelonisMathFunctionsTest, square_bigint_input) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    values->append_datum(kNullDatum);
    values->append_datum(0L);
    values->append_datum(100L);
    values->append_datum(7L);
    values->append_datum(-25L);
    values->append_datum(3037000501L); // overflow
    values->append_datum(kNullDatum);
    const auto result = CelonisMathFunctions<TYPE_BIGINT>::square(nullptr, {values}).value();
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(10000L, result->get(2).get_int64());
    EXPECT_EQ(49L, result->get(3).get_int64());
    EXPECT_EQ(625L, result->get(4).get_int64());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_TRUE(result->get(6).is_null());
}

TEST_F(CelonisMathFunctionsTest, square_double_input) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);
    values->append_datum(kNullDatum);
    values->append_datum(0.0);
    values->append_datum(2.5);
    values->append_datum(12.0);
    values->append_datum(-2.5);
    values->append_datum(std::sqrt(std::numeric_limits<double>::max()) * 1.05);
    values->append_datum(kNullDatum);
    const auto result = CelonisMathFunctions<TYPE_DOUBLE>::square(nullptr, {values}).value();
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(0L, result->get(1).get_double());
    EXPECT_EQ(6.25, result->get(2).get_double());
    EXPECT_EQ(144, result->get(3).get_double());
    EXPECT_EQ(6.25, result->get(4).get_double());
    EXPECT_TRUE(std::isinf(result->get(5).get_double()));
    EXPECT_TRUE(result->get(6).is_null());
}

} // namespace starrocks
