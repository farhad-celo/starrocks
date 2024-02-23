#include "exprs/celonis/qnorm.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisQnormTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(CelonisQnormTest, empty_input) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);
    const auto result = CelonisQnorm::qnorm(nullptr, {values}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisQnormTest, normal_cases) {
    auto values = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);
    values->append_datum(kNullDatum);
    values->append_datum(1.0);
    values->append_datum(2.5);
    values->append_datum(0.0);
    values->append_datum(-1.2);
    values->append_datum(1.0E-7);
    values->append_datum(1.0E-5);
    values->append_datum(0.001);
    values->append_datum(0.05);
    values->append_datum(0.15);
    values->append_datum(0.25);
    values->append_datum(0.35);
    values->append_datum(0.45);
    values->append_datum(0.55);
    values->append_datum(0.65);
    values->append_datum(0.75);
    values->append_datum(0.85);
    values->append_datum(0.95);
    values->append_datum(0.999);
    values->append_datum(0.99999);
    values->append_datum(0.9999999);
    values->append_datum(kNullDatum);
    const auto result = CelonisQnorm::qnorm(nullptr, {values}).value();
    const double abs_error = 0.0000001;
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_NEAR(-5.1993376, result->get(5).get_double(), abs_error);
    EXPECT_NEAR(-4.2648908, result->get(6).get_double(), abs_error);
    EXPECT_NEAR(-3.0902323, result->get(7).get_double(), abs_error);
    EXPECT_NEAR(-1.6448536, result->get(8).get_double(), abs_error);
    EXPECT_NEAR(-1.0364334, result->get(9).get_double(), abs_error);
    EXPECT_NEAR(-0.6744898, result->get(10).get_double(), abs_error);
    EXPECT_NEAR(-0.3853205, result->get(11).get_double(), abs_error);
    EXPECT_NEAR(-0.1256613, result->get(12).get_double(), abs_error);
    EXPECT_NEAR(0.1256613, result->get(13).get_double(), abs_error);
    EXPECT_NEAR(0.3853205, result->get(14).get_double(), abs_error);
    EXPECT_NEAR(0.6744898, result->get(15).get_double(), abs_error);
    EXPECT_NEAR(1.0364334, result->get(16).get_double(), abs_error);
    EXPECT_NEAR(1.6448536, result->get(17).get_double(), abs_error);
    EXPECT_NEAR(3.0902323, result->get(18).get_double(), abs_error);
    EXPECT_NEAR(4.2648908, result->get(19).get_double(), abs_error);
    EXPECT_NEAR(5.1993376, result->get(20).get_double(), abs_error);
    EXPECT_TRUE(result->get(21).is_null());
}

} // namespace starrocks
