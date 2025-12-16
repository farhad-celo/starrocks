#include "exprs/celonis/kmeans.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisApplyKMeansModelTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);

    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                FunctionContext::TypeDesc{TYPE_ARRAY},
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));
        point_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_DOUBLE), true);
        model_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    void AddRow(const std::optional<DatumArray>& point, const Datum& model) {
        if (point.has_value()) {
            point_column_->append_datum(point.value());
        } else {
            point_column_->append_datum(kNullDatum);
        }
        model_column_->append_datum(model);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] { CelonisKmeans::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisKmeans::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisKmeans::close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisKmeans::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result = CelonisKmeans::apply_kmeans_model(ctx_.get(), {point_column_, model_column_});
        return result;
    }

    StatusOr<ColumnPtr> RunConstantModel(const Datum& model) {
        model_column_->append_datum(model);
        const auto nrows = point_column_->size();
        model_column_ = ConstColumn::create(model_column_, nrows);
        ctx_->set_constant_columns({nullptr, model_column_});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr point_column_;
    ColumnPtr model_column_;
};

TEST_F(CelonisApplyKMeansModelTest, empty_input) {
    Prepare();
    const auto result = RunConstantModel("0,10:0.0;1.0").value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisApplyKMeansModelTest, null_const_column) {
    {
        Prepare();
        point_column_->append_datum(kNullDatum);
        point_column_ = ConstColumn::create(point_column_, 2);
        const auto result = RunConstantModel("0,10;0,20:0.2,0.25;0.12,0.13").value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        Prepare();
        point_column_->append_datum(kNullDatum);
        point_column_ = ConstColumn::create(point_column_, 2);
        model_column_->append_datum("0,10;0,10:0.2,0.25");
        model_column_->append_datum("0,10;0,10:0.2,0.25");
        const auto result = Run().value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisApplyKMeansModelTest, single_feature_const_valid_model) {
    Prepare();
    point_column_->append_datum(DatumArray{kNullDatum});
    point_column_->append_datum(DatumArray{2.0});
    point_column_->append_datum(DatumArray{4.0});
    point_column_->append_datum(DatumArray{1.5});
    point_column_->append_datum(DatumArray{kNullDatum});
    point_column_->append_datum(DatumArray{0.0});
    point_column_->append_datum(DatumArray{10.0});
    point_column_->append_datum(DatumArray{kNullDatum});
    point_column_->append_datum(DatumArray{3.0});

    const auto result = RunConstantModel("0,10:0.1;0.5").value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_TRUE(result->get(7).is_null());
    EXPECT_EQ(0L, result->get(8).get_int64());
}

TEST_F(CelonisApplyKMeansModelTest, single_feature_const_invalid_model) {
    Prepare();
    point_column_->append_datum(DatumArray{kNullDatum});
    point_column_->append_datum(DatumArray{2.0});
    point_column_->append_datum(DatumArray{4.0});
    point_column_->append_datum(DatumArray{1.5});
    point_column_->append_datum(DatumArray{kNullDatum});
    point_column_->append_datum(DatumArray{0.0});
    point_column_->append_datum(DatumArray{10.0});
    point_column_->append_datum(DatumArray{kNullDatum});
    const auto result = RunConstantModel("1.0%0.1").value();
    ASSERT_EQ(point_column_->size(), result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisApplyKMeansModelTest, single_feature_nonconst_model) {
    Prepare();
    AddRow(DatumArray{2.0}, "0,10:0.2;0.25");
    AddRow(DatumArray{4.0}, "0,10:0.35;0.40");
    AddRow(DatumArray{kNullDatum}, "0,10:0.10;0.2");
    AddRow(DatumArray{2.5}, "0,10:0.35;0.4");
    AddRow(DatumArray{2.5}, "0,10:3.5#4.0");
    AddRow(DatumArray{0.5}, "HELLO");
    AddRow(DatumArray{kNullDatum}, kNullDatum);
    AddRow(DatumArray{0.0}, "0,10:0.15;0.93");
    const auto result = Run().value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_TRUE(result->get(6).is_null());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisApplyKMeansModelTest, double_features_const_valid_model) {
    Prepare();
    // NULL 1.2
    // 1.5 2.5
    // 2.0 4.5
    // NULL NULL
    // 3.0 4.5
    // 1.2 NULL
    // 0.6 2.0
    point_column_->append_datum(DatumArray{kNullDatum, 1.2});
    point_column_->append_datum(DatumArray{1.5, 2.5});
    point_column_->append_datum(DatumArray{2.0, 4.5});
    point_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    point_column_->append_datum(DatumArray{3.0, 4.5});
    point_column_->append_datum(DatumArray{1.2, kNullDatum});
    point_column_->append_datum(DatumArray{0.6, 2.0});
    const auto result = RunConstantModel("0,10;0,10:0.2,0.25;0.05,0.2").value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_EQ(1L, result->get(6).get_int64());
}

TEST_F(CelonisApplyKMeansModelTest, double_features_const_invalid_model) {
    Prepare();
    // NULL 1.2
    // 1.5 2.5
    // 2.0 4.5
    // NULL NULL
    // 3.0 4.5
    // 1.2 NULL
    point_column_->append_datum(DatumArray{kNullDatum, 1.2});
    point_column_->append_datum(DatumArray{1.5, 2.5});
    point_column_->append_datum(DatumArray{2.0, 4.5});
    point_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    point_column_->append_datum(DatumArray{3.0, 4.5});
    point_column_->append_datum(DatumArray{1.2, kNullDatum});
    const auto result = RunConstantModel(":4.0,2.0,2.5;0.5").value();
    ASSERT_EQ(point_column_->size(), result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisApplyKMeansModelTest, double_features_nonconst_model) {
    Prepare();
    AddRow(DatumArray{2.0, 3.0}, "0,10;0,10:0.20,0.35;0.15,0.20;0.20,0.30");
    AddRow(DatumArray{3.0, 2.5}, "3.5:4.0");
    AddRow(DatumArray{kNullDatum, 3.0}, "1.0:2.0:1.5");
    AddRow(DatumArray{0.5, 1.5}, "HELLO");
    AddRow(DatumArray{1.0, 2.2}, kNullDatum);
    AddRow(DatumArray{0.0, 1.5}, "0,10;0,10:0.15,0.93;0.24,0.35");
    const auto result = Run().value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_EQ(2L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(1L, result->get(5).get_int64());
}

TEST_F(CelonisApplyKMeansModelTest, three_features_const_valid_model) {
    Prepare();
    // 2.0 2.4 0.5
    // 1.5 2.5 2.0
    // 2.0 4.5 3.0
    point_column_->append_datum(DatumArray{2.0, 2.4, 0.5});
    point_column_->append_datum(DatumArray{1.5, 2.5, 2.0});
    point_column_->append_datum(DatumArray{2.0, 4.5, 3.0});
    const auto result = RunConstantModel("0,10;0,10;0,10:0.20,0.25,0.05;0.01,0.25,0.20").value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

TEST_F(CelonisApplyKMeansModelTest, null_point) {
    {
        Prepare();
        point_column_->append_datum(kNullDatum);
        point_column_->append_datum(DatumArray{0.2});
        const auto result = RunConstantModel("0,1:0.0;1.0").value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
    {
        Prepare();
        AddRow(DatumArray{0.0, 1.5}, "0,10;0,10:0.15,0.93;0.24,0.35");
        AddRow(std::nullopt, "0,1:0.0;1.0");
        const auto result = Run().value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisApplyKMeansModelTest, model_dimension_inconsistent_with_point_dimension) {
    {
        Prepare();
        point_column_->append_datum(DatumArray{0.1, 0.2});
        point_column_->append_datum(DatumArray{0.2});
        const auto result = RunConstantModel("0,10:0.0;1.0").value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
    {
        Prepare();
        AddRow(DatumArray{0.0, 1.5}, "0,10;1,10:0.15,0.93;0.24,0.35");
        AddRow(DatumArray{0.0, 1.2}, "0,10:0.0;1.0");
        const auto result = Run().value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

} // namespace starrocks
