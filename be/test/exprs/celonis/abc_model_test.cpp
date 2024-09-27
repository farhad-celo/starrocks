#include "exprs/celonis/abc_model.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisAbcModelTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    template<LogicalType LT>
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(LT)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        value_column_ = ColumnHelper::create_column(TypeDescriptor(LT), true);
        pk_hash_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        model_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    void
    AddRow(const Datum& value, const Datum& pk_hash, const Datum& model) {
        value_column_->append_datum(value);
        pk_hash_column_->append_datum(pk_hash);
        model_column_->append_datum(model);
    }

    template<LogicalType LT>
    StatusOr<ColumnPtr> Run() {
        RETURN_IF_ERROR(CelonisAbcModel<LT>::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        RETURN_IF_ERROR(CelonisAbcModel<LT>::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisAbcModel<LT>::apply_abc_model(ctx_.get(), {value_column_, pk_hash_column_, model_column_});
        RETURN_IF_ERROR(CelonisAbcModel<LT>::close(ctx_.get(), FunctionContext::THREAD_LOCAL));
        RETURN_IF_ERROR(CelonisAbcModel<LT>::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        return result;
    }

    template<LogicalType LT>
    StatusOr<ColumnPtr>
    RunConstantModel(const Datum& model) {
        model_column_->append_datum(model);
        const auto nrows = value_column_->size();
        model_column_ = ConstColumn::create(model_column_, nrows);
        ctx_->set_constant_columns({nullptr, nullptr, model_column_});
        return Run<LT>();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr value_column_;
    ColumnPtr pk_hash_column_;
    ColumnPtr model_column_;
};

TEST_F(CelonisAbcModelTest, empty_input) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();
    const auto result = Run<LT>().value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisAbcModelTest, null_input) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();
    AddRow(10L, 1L, "7,10,5,6,1,4:");
    AddRow(kNullDatum, 2L, "7,10,5,6,1,4:");
    AddRow(1L, kNullDatum, "7,10,5,6,1,4:");
    AddRow(1L, 3L, kNullDatum);
    AddRow(kNullDatum, 4L, kNullDatum);
    AddRow(kNullDatum, kNullDatum, kNullDatum);
    AddRow(kNullDatum, kNullDatum, "7,10,5,6,1,4:");
    AddRow(1L, 5L, "7,10,5,6,1,4:");
    const auto result = Run<LT>().value();
    EXPECT_EQ(value_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_TRUE(result->get(5).is_null());
    EXPECT_TRUE(result->get(6).is_null());
    EXPECT_EQ(3L, result->get(7).get_int64());
}

TEST_F(CelonisAbcModelTest, invalid_models) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();
    AddRow(10L, 1L, "7,10,56,1,4:");
    AddRow(10L, 2L, "7,10,5,6,1,4,6:");
    AddRow(10L, 3L, "7,10,5,6,1,4");
    AddRow(10L, 4L, "7,10,5,6,1,4:5");
    AddRow(10L, 5L, "7,10,5,6,1,4:5,0.2,0.4");
    AddRow(10L, 6L, "7,10,5,6,1,4:5,0.2,0.4,0.4:");
    AddRow(10L, 7L, "7,10,5,6,1,4:5,0.2,0.4,0.4:0.3");
    AddRow(10L, 8L, "7,10,5,6,1,4:5,0.2,0.4,1.2");
    AddRow(10L, 8L, "7.0,10,5,6,1,4:");
    const auto result = Run<LT>().value();
    EXPECT_EQ(value_column_->size(), result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisAbcModelTest, bigint_input_with_valid_const_model) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();

    for (int64_t v = 1; v <= 10; ++v) {
        value_column_->append_datum(v);
        pk_hash_column_->append_datum(v);
    }
    const auto result = RunConstantModel<LT>("7,10,5,6,1,4:").value();
    ASSERT_EQ(value_column_->size(), result->size());
    EXPECT_EQ(3L, result->get(0).get_int64());
    EXPECT_EQ(3L, result->get(1).get_int64());
    EXPECT_EQ(3L, result->get(2).get_int64());
    EXPECT_EQ(3L, result->get(3).get_int64());
    EXPECT_EQ(2L, result->get(4).get_int64());
    EXPECT_EQ(2L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(1L, result->get(7).get_int64());
    EXPECT_EQ(1L, result->get(8).get_int64());
    EXPECT_EQ(1L, result->get(9).get_int64());
}

TEST_F(CelonisAbcModelTest, bigint_input_const_model_with_empty_ranges) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();

    for (int64_t v = 1; v <= 10; ++v) {
        value_column_->append_datum(v);
        pk_hash_column_->append_datum(v);
    }
    const auto result = RunConstantModel<LT>("7,10,5,6,0,-1:").value();
    ASSERT_EQ(value_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(2L, result->get(4).get_int64());
    EXPECT_EQ(2L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(1L, result->get(7).get_int64());
    EXPECT_EQ(1L, result->get(8).get_int64());
    EXPECT_EQ(1L, result->get(9).get_int64());
}

TEST_F(CelonisAbcModelTest, bigint_input_with_valid_const_model_using_probs) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();
    // 10 7, 7 5 5, 5 5 3 3 3
    value_column_->append_datum(10L);
    value_column_->append_datum(7L);
    value_column_->append_datum(7L);
    value_column_->append_datum(5L);
    value_column_->append_datum(5L);
    value_column_->append_datum(5L);
    value_column_->append_datum(5L);
    value_column_->append_datum(3L);
    value_column_->append_datum(3L);
    value_column_->append_datum(3L);
    pk_hash_column_->append_datum(3286617070807834082L);
    pk_hash_column_->append_datum(83702769564653161L);
    pk_hash_column_->append_datum(8354489157789155007L);
    pk_hash_column_->append_datum(4788497620020407012L);
    pk_hash_column_->append_datum(720386602316994774L);
    pk_hash_column_->append_datum(8896289486892991094L);
    pk_hash_column_->append_datum(100653895908812942L);
    pk_hash_column_->append_datum(3280422888343003963L);
    pk_hash_column_->append_datum(7711011697350452960L);
    pk_hash_column_->append_datum(439907815211755200L);

    const auto result = RunConstantModel<LT>("7,10,5,7,3,5:7,0.5,0.5,0.0;5,0.0,0.5,0.5").value();
    ASSERT_EQ(value_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_EQ(3L, result->get(3).get_int64());
    EXPECT_EQ(2L, result->get(4).get_int64());
    EXPECT_EQ(3L, result->get(5).get_int64());
    EXPECT_EQ(2L, result->get(6).get_int64());
    EXPECT_EQ(3L, result->get(7).get_int64());
    EXPECT_EQ(3L, result->get(8).get_int64());
    EXPECT_EQ(3L, result->get(9).get_int64());
}

TEST_F(CelonisAbcModelTest, bigint_input_with_invalid_const_model) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();

    for (int64_t v = 1; v <= 10; ++v) {
        value_column_->append_datum(v);
        pk_hash_column_->append_datum(v);
    }
    // 7.5 is a double
    const auto result = RunConstantModel<LT>("7.5,10,5,6,1,4:").value();
    ASSERT_EQ(10, result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

TEST_F(CelonisAbcModelTest, bigint_input_with_nonconst_model) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();
    AddRow(1L, 1L, "7,10,5,6,1,4:");
    AddRow(7L, kNullDatum, "7,10,5,6,1,4:");
    AddRow(10L, 2L, "7,12,5,6,1,4:");
    AddRow(kNullDatum, 3L, "7,10,5,6,1,4:");
    AddRow(5L, 4L, "7,10,5,6,1,4:");
    const auto result = Run<LT>().value();
    ASSERT_EQ(value_column_->size(), result->size());
    EXPECT_EQ(3L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(2L, result->get(4).get_int64());
}

TEST_F(CelonisAbcModelTest, double_input_with_valid_const_model) {
    const LogicalType LT = TYPE_DOUBLE;
    Prepare<LT>();

    for (auto i = 1; i <= 10; ++i) {
        value_column_->append_datum(i + 0.5);
        pk_hash_column_->append_datum(static_cast<int64_t>(i));
    }
    const auto result = RunConstantModel<LT>("7.5,10.5,5.5,6.5,1.5,4.5:").value();
    ASSERT_EQ(value_column_->size(), result->size());
    EXPECT_EQ(3L, result->get(0).get_int64());
    EXPECT_EQ(3L, result->get(1).get_int64());
    EXPECT_EQ(3L, result->get(2).get_int64());
    EXPECT_EQ(3L, result->get(3).get_int64());
    EXPECT_EQ(2L, result->get(4).get_int64());
    EXPECT_EQ(2L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(1L, result->get(7).get_int64());
    EXPECT_EQ(1L, result->get(8).get_int64());
    EXPECT_EQ(1L, result->get(9).get_int64());
}

TEST_F(CelonisAbcModelTest, double_input_with_valid_const_model_using_probs) {
    const LogicalType LT = TYPE_DOUBLE;
    Prepare<LT>();
    // 10.5 7.5, 7.5 5.5 5.5, 5.5 5.5 3.5 3.5 3.5
    value_column_->append_datum(10.5);
    value_column_->append_datum(7.5);
    value_column_->append_datum(7.5);
    value_column_->append_datum(5.5);
    value_column_->append_datum(5.5);
    value_column_->append_datum(5.5);
    value_column_->append_datum(5.5);
    value_column_->append_datum(3.5);
    value_column_->append_datum(3.5);
    value_column_->append_datum(3.5);
    pk_hash_column_->append_datum(3286617070807834082L);
    pk_hash_column_->append_datum(83702769564653161L);
    pk_hash_column_->append_datum(8354489157789155007L);
    pk_hash_column_->append_datum(4788497620020407012L);
    pk_hash_column_->append_datum(-720386602316994774L);
    pk_hash_column_->append_datum(8896289486892991094L);
    pk_hash_column_->append_datum(100653895908812942L);
    pk_hash_column_->append_datum(3280422888343003963L);
    pk_hash_column_->append_datum(7711011697350452960L);
    pk_hash_column_->append_datum(439907815211755200L);

    const auto result = RunConstantModel<LT>("7.5,10.5,5.5,7.5,3.5,5.5:7.5,0.5,0.5,0.0;5.5,0.0,0.5,0.5").value();
    ASSERT_EQ(value_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(2L, result->get(2).get_int64());
    EXPECT_EQ(3L, result->get(3).get_int64());
    EXPECT_EQ(2L, result->get(4).get_int64());
    EXPECT_EQ(3L, result->get(5).get_int64());
    EXPECT_EQ(2L, result->get(6).get_int64());
    EXPECT_EQ(3L, result->get(7).get_int64());
    EXPECT_EQ(3L, result->get(8).get_int64());
    EXPECT_EQ(3L, result->get(9).get_int64());
}

TEST_F(CelonisAbcModelTest, double_input_with_nonconst_model) {
    const LogicalType LT = TYPE_DOUBLE;
    Prepare<LT>();
    AddRow(1.5, 1L, "7,10,5,6,1,4:");
    AddRow(7.5, kNullDatum, "7,10,5,6,1,4:");
    AddRow(10.5, 2L, "7,12,5,6,1,4:");
    AddRow(kNullDatum, 3L, "7,10,5,6,1,4:");
    AddRow(5.5, 4L, "7,10,5,6,1,4:");
    AddRow(15.5, 5L, "7,10,5,6,1,4:");
    const auto result = Run<LT>().value();
    ASSERT_EQ(value_column_->size(), result->size());
    EXPECT_EQ(3L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(2L, result->get(4).get_int64());
    EXPECT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisAbcModelTest, double_input_with_invalid_const_model) {
    const LogicalType LT = TYPE_DOUBLE;
    Prepare<LT>();

    for (auto i = 1; i <= 10; ++i) {
        value_column_->append_datum(i + 0.5);
        pk_hash_column_->append_datum(static_cast<int64_t>(i));
    }
    const auto result = RunConstantModel<LT>("7.5,10.5,5.5,6.5,1.5,4.5:hello").value();
    ASSERT_EQ(value_column_->size(), result->size());
    for (auto i = 0; i < result->size(); ++i) {
        EXPECT_TRUE(result->get(i).is_null());
    }
}

} // namespace starrocks
