#include "exprs/celonis/qnorm.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <boost/math/distributions/normal.hpp>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"

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
    const double mean = 0.0;
    const double std_dev = 1.0;
    boost::math::normal_distribution<double> dist(mean, std_dev);
    const auto result = CelonisQnorm::qnorm(nullptr, {values}).value();
    const double abs_error = 0.00005;
    EXPECT_EQ(values->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_TRUE(result->get(4).is_null());
    for (size_t i = 5; i < 21; ++i) {
        EXPECT_NEAR(boost::math::quantile(dist, values->get(i).get_double()), result->get(i).get_double(), abs_error);
    }
    EXPECT_TRUE(result->get(21).is_null());
}

} // namespace starrocks
