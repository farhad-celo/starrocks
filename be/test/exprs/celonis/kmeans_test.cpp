#include "exprs/celonis/kmeans.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisKmeansTest : public ::testing::Test {
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
        point_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_ARRAY_DOUBLE), true);
        model_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    void
    AddRow(const DatumArray& point, const Datum& model) {
        point_column_->append_datum(point);
        model_column_->append_datum(model);
    }

    StatusOr<ColumnPtr> Run() {
        RETURN_IF_ERROR(CelonisKmeans::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        RETURN_IF_ERROR(CelonisKmeans::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result = CelonisKmeans::apply_kmeans_model(ctx_.get(),
                                                                       {point_column_, model_column_});
        RETURN_IF_ERROR(CelonisKmeans::close(ctx_.get(), FunctionContext::THREAD_LOCAL));
        RETURN_IF_ERROR(CelonisKmeans::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        return result;
    }

    StatusOr<ColumnPtr>
    RunConstantModel(const Datum& model) {
        model_column_->append_datum(model);
        ctx_->set_constant_columns({nullptr, model_column_});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr point_column_;
    ColumnPtr model_column_;
};

TEST_F(CelonisKmeansTest, null_const_column) {
    {
        Prepare();
        point_column_->append_datum(kNullDatum);
        point_column_ = ConstColumn::create(point_column_, 2);
        const auto result = RunConstantModel("2.0,2.5;1.2,1.3").value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        Prepare();
        point_column_->append_datum(kNullDatum);
        point_column_ = ConstColumn::create(point_column_, 2);
        model_column_->append_datum("2.0,2.5");
        model_column_->append_datum("2.0,2.5");
        const auto result = Run().value();
        ASSERT_EQ(point_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisKmeansTest, single_feature_const_valid_model) {
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

    const auto result = RunConstantModel("1.0;5.0").value();
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

TEST_F(CelonisKmeansTest, single_feature_const_invalid_model) {
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

TEST_F(CelonisKmeansTest, single_feature_nonconst_model) {
    Prepare();
    AddRow({2.0}, "2.0;2.5");
    AddRow({4.0}, "3.5;4.0");
    AddRow({kNullDatum}, "1.0;2.0");
    AddRow({2.5}, "3.5;4.0");
    AddRow({2.5}, "3.5#4.0");
    AddRow({0.5}, "HELLO");
    AddRow({kNullDatum}, kNullDatum);
    AddRow({0.0}, "1.5;9.3");
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

TEST_F(CelonisKmeansTest, double_features_const_valid_model) {
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
    const auto result = RunConstantModel("2.0,2.5;0.5,2.0").value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_EQ(1L, result->get(6).get_int64());
}

TEST_F(CelonisKmeansTest, double_features_const_invalid_model) {
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
    const auto result = RunConstantModel("2.0,2.5;0.5").value();
    ASSERT_EQ(point_column_->size(), result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisKmeansTest, double_features_nonconst_model) {
    Prepare();
    AddRow({2.0, 3.0}, "2.0,3.5;1.5,2.0;2.0,3.0");
    AddRow({3.0, 2.5}, "3.5:4.0");
    AddRow({kNullDatum, 3.0}, "1.0:2.0:1.5");
    AddRow({0.5, 1.5}, "HELLO");
    AddRow({1.0, 2.2}, kNullDatum);
    AddRow({0.0, 1.5}, "1.5,9.3;2.4,3.5");
    const auto result = Run().value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_EQ(2L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(1L, result->get(5).get_int64());
}

TEST_F(CelonisKmeansTest, three_features_const_valid_model) {
    Prepare();
    // 2.0 2.4 0.5
    // 1.5 2.5 2.0
    // 2.0 4.5 3.0
    point_column_->append_datum(DatumArray{2.0, 2.4, 0.5});
    point_column_->append_datum(DatumArray{1.5, 2.5, 2.0});
    point_column_->append_datum(DatumArray{2.0, 4.5, 3.0});
    const auto result = RunConstantModel("2.0,2.5,0.5;0.1,2.5,2.0").value();
    ASSERT_EQ(point_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

} // namespace starrocks
