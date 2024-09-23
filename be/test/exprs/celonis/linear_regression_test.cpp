#include "exprs/celonis/linear_regression.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisLinearRegressionTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);

    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                FunctionContext::TypeDesc{TYPE_ARRAY},
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));
        x_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_DOUBLE), true);
        model_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    void
    AddRow(const DatumArray& xs, const Datum& model) {
        x_column_->append_datum(xs);
        model_column_->append_datum(model);
    }

    StatusOr<ColumnPtr> Run() {
        RETURN_IF_ERROR(CelonisLinearRegression::predict_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        RETURN_IF_ERROR(CelonisLinearRegression::predict_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result = CelonisLinearRegression::predict_linear_regression(ctx_.get(),
                                                                                        {x_column_, model_column_});
        RETURN_IF_ERROR(CelonisLinearRegression::predict_close(ctx_.get(), FunctionContext::THREAD_LOCAL));
        RETURN_IF_ERROR(CelonisLinearRegression::predict_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        return result;
    }

    StatusOr<ColumnPtr>
    RunConstantModel(const Datum& model) {
        model_column_->append_datum(model);
        const auto nrows = x_column_->size();
        ctx_->set_constant_columns({nullptr, ConstColumn::create(model_column_, nrows)});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr x_column_;
    ColumnPtr model_column_;
};

TEST_F(CelonisLinearRegressionTest, null_const_column) {
    {
        Prepare();
        x_column_->append_datum(kNullDatum);
        x_column_ = ConstColumn::create(x_column_, 2);
        const auto result = RunConstantModel("2.0:2.5").value();
        ASSERT_EQ(x_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        Prepare();
        x_column_->append_datum(kNullDatum);
        x_column_ = ConstColumn::create(x_column_, 2);
        model_column_->append_datum("2.0:2.5");
        model_column_->append_datum("2.0:2.5");
        const auto result = Run().value();
        ASSERT_EQ(x_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisLinearRegressionTest, single_feature_const_valid_model) {
    Prepare();
    x_column_->append_datum(DatumArray{kNullDatum});
    x_column_->append_datum(DatumArray{2.0});
    x_column_->append_datum(DatumArray{4.0});
    x_column_->append_datum(DatumArray{1.5});
    x_column_->append_datum(DatumArray{kNullDatum});
    x_column_->append_datum(DatumArray{0.0});
    x_column_->append_datum(DatumArray{10.0});
    x_column_->append_datum(DatumArray{kNullDatum});

    const auto result = RunConstantModel("2.0:2.5").value();
    ASSERT_EQ(x_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(7.0, result->get(1).get_double());
    EXPECT_EQ(12.0, result->get(2).get_double());
    EXPECT_EQ(5.75, result->get(3).get_double());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(2.0, result->get(5).get_double());
    EXPECT_EQ(27.0, result->get(6).get_double());
    EXPECT_TRUE(result->get(7).is_null());
}

TEST_F(CelonisLinearRegressionTest, single_feature_const_invalid_model) {
    Prepare();
    x_column_->append_datum(DatumArray{kNullDatum});
    x_column_->append_datum(DatumArray{2.0});
    x_column_->append_datum(DatumArray{4.0});
    x_column_->append_datum(DatumArray{1.5});
    x_column_->append_datum(DatumArray{kNullDatum});
    x_column_->append_datum(DatumArray{0.0});
    x_column_->append_datum(DatumArray{10.0});
    x_column_->append_datum(DatumArray{kNullDatum});
    const auto result = RunConstantModel("1.0%0.1").value();
    ASSERT_EQ(x_column_->size(), result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisLinearRegressionTest, single_feature_nonconst_model) {
    Prepare();
    AddRow({2.0}, "2.0:2.5");
    AddRow({3.0}, "3.5:4.0");
    AddRow({kNullDatum}, "1.0:2.0");
    AddRow({2.5}, "3.5:4.0");
    AddRow({2.5}, "3.5#4.0");
    AddRow({0.5}, "HELLO");
    AddRow({kNullDatum}, kNullDatum);
    AddRow({0.0}, "1.5:9.3");
    AddRow({std::numeric_limits<double>::quiet_NaN()}, "1.2:3.4");
    const auto result = Run().value();
    ASSERT_EQ(x_column_->size(), result->size());
    EXPECT_EQ(7.0, result->get(0).get_double());
    EXPECT_EQ(15.5, result->get(1).get_double());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ(13.5, result->get(3).get_double());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_TRUE(result->get(6).is_null());
    EXPECT_EQ(1.5, result->get(7).get_double());
    EXPECT_TRUE(std::isnan(result->get(8).get_double()));
}

TEST_F(CelonisLinearRegressionTest, double_features_const_valid_model) {
    Prepare();
    // NULL 1.2
    // 1.5 2.5
    // 2.0 4.5
    // NULL NULL
    // 3.0 4.5
    // 1.2 NULL
    x_column_->append_datum(DatumArray{kNullDatum, 1.2});
    x_column_->append_datum(DatumArray{1.5, 2.5});
    x_column_->append_datum(DatumArray{2.0, 4.5});
    x_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    x_column_->append_datum(DatumArray{3.0, 4.5});
    x_column_->append_datum(DatumArray{1.2, kNullDatum});
    const auto result = RunConstantModel("2.0:2.5:0.5").value();
    ASSERT_EQ(x_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(7.0, result->get(1).get_double());
    EXPECT_EQ(9.25, result->get(2).get_double());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(11.75, result->get(4).get_double());
    EXPECT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisLinearRegressionTest, double_features_const_invalid_model) {
    Prepare();
    // NULL 1.2
    // 1.5 2.5
    // 2.0 4.5
    // NULL NULL
    // 3.0 4.5
    // 1.2 NULL
    x_column_->append_datum(DatumArray{kNullDatum, 1.2});
    x_column_->append_datum(DatumArray{1.5, 2.5});
    x_column_->append_datum(DatumArray{2.0, 4.5});
    x_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    x_column_->append_datum(DatumArray{3.0, 4.5});
    x_column_->append_datum(DatumArray{1.2, kNullDatum});
    const auto result = RunConstantModel("2.0:2.5%0.5").value();
    ASSERT_EQ(x_column_->size(), result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisLinearRegressionTest, double_features_nonconst_model) {
    Prepare();
    AddRow({2.0, 3.0}, "2.0:2.5:1.5");
    AddRow({3.0, 2.5}, "3.5:4.0");
    AddRow({kNullDatum, 3.0}, "1.0:2.0:1.5");
    AddRow({0.5, 1.5}, "HELLO");
    AddRow({1.0, 2.2}, kNullDatum);
    AddRow({0.0, 1.5}, "1.5:9.3:2.4");
    const auto result = Run().value();
    ASSERT_EQ(x_column_->size(), result->size());
    EXPECT_EQ(11.5, result->get(0).get_double());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(5.1, result->get(5).get_double());
}

TEST_F(CelonisLinearRegressionTest, three_features_const_valid_model) {
    Prepare();
    // 1.5 2.5 2.0
    // 2.0 4.5 3.0
    x_column_->append_datum(DatumArray{1.5, 2.5, 2.0});
    x_column_->append_datum(DatumArray{2.0, 4.5, 3.0});
    const auto result = RunConstantModel("2.0:2.5:0.5:0.1").value();
    ASSERT_EQ(x_column_->size(), result->size());
    EXPECT_EQ(7.2, result->get(0).get_double());
    EXPECT_EQ(9.55, result->get(1).get_double());
}

} // namespace starrocks
