#include "exprs/celonis/encode_variant.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <utility>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisEncodeVariantTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_ARRAY),
                                                            TypeDescriptor::from_logical_type(TYPE_ARRAY)};
        auto return_type = TypeDescriptor::from_logical_type(TYPE_ARRAY);
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        variant_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        activity_array_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
    }

    void AddRow(const std::optional<DatumArray>& variant, const std::optional<DatumArray>& activity_array) {
        if (variant.has_value()) {
            variant_column_->append_datum(variant.value());
        } else {
            variant_column_->append_datum(kNullDatum);
        }
        if (activity_array.has_value()) {
            activity_array_column_->append_datum(activity_array.value());
        } else {
            activity_array_column_->append_datum(kNullDatum);
        }
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local(
                [this] { CelonisEncodeVariant::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisEncodeVariant::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisEncodeVariant::close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisEncodeVariant::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        auto result = CelonisEncodeVariant::encode_variant(ctx_.get(), {variant_column_, activity_array_column_});
        return result;
    }

    StatusOr<ColumnPtr> RunConstantMap(const std::optional<DatumArray>& activity_array) {
        if (activity_array.has_value()) {
            activity_array_column_->append_datum(activity_array.value());
        } else {
            activity_array_column_->append_datum(kNullDatum);
        }
        const auto num_rows = variant_column_->size();
        activity_array_column_ = ConstColumn::create(activity_array_column_, num_rows);
        ctx_->set_constant_columns({nullptr, activity_array_column_});
        return Run();
    }

    void Validate(const ColumnPtr& result, size_t row, const std::optional<std::vector<int32_t>>& expected) {
        ASSERT_LT(row, result->size());
        if (!expected.has_value()) {
            EXPECT_TRUE(result->is_null(row));
        } else {
            auto array = result->get(row).get_array();
            ASSERT_EQ(array.size(), expected->size());
            for (auto i = 0; i < expected->size(); ++i) {
                EXPECT_EQ(expected->at(i), array[i].get_int32());
            }
        }
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr variant_column_;
    ColumnPtr activity_array_column_;
};

TEST_F(CelonisEncodeVariantTest, const_activity_array_normal_case) {
    Prepare();

    variant_column_->append_datum(DatumArray{"A1", "B2"});
    variant_column_->append_datum(DatumArray{"A1", "B2", "A1", "B2"});
    variant_column_->append_datum(DatumArray{"A1", "A1", "B2"});
    variant_column_->append_datum(DatumArray{"A1", kNullDatum, "A1", "B2", kNullDatum});
    variant_column_->append_datum(kNullDatum);

    auto activity_array = DatumArray{"A1", kNullDatum, "B2", "C3", "D4", kNullDatum};

    const auto result = RunConstantMap(activity_array).value();
    ASSERT_EQ(variant_column_->size(), result->size());
    Validate(result, 0, std::vector<int32_t>{0, 1});
    Validate(result, 1, std::vector<int32_t>{0, 1, 0, 1});
    Validate(result, 2, std::vector<int32_t>{0, 0, 1});
    Validate(result, 3, std::vector<int32_t>{0, 0, 1});
    Validate(result, 4, std::nullopt);
}

TEST_F(CelonisEncodeVariantTest, null_activity_array) {
    Prepare();

    variant_column_->append_datum(DatumArray{"A1", "B2"});
    variant_column_->append_datum(DatumArray{"A1", kNullDatum, "A1", "B2", kNullDatum});
    variant_column_->append_datum(kNullDatum);

    // NULL activity_array
    const auto result = RunConstantMap(std::nullopt).value();
    ASSERT_EQ(variant_column_->size(), result->size());
    Validate(result, 0, std::nullopt);
    Validate(result, 1, std::nullopt);
    Validate(result, 2, std::nullopt);
}

TEST_F(CelonisEncodeVariantTest, const_activity_array_duplicates_in_array_activity) {
    Prepare();

    variant_column_->append_datum(DatumArray{"A1", "B2"});
    variant_column_->append_datum(DatumArray{"A1", "B2", "A1", "B2"});
    variant_column_->append_datum(DatumArray{"A1", "A1", "B2"});
    variant_column_->append_datum(DatumArray{"A1", kNullDatum, "A1", "B2", kNullDatum});
    variant_column_->append_datum(kNullDatum);
    variant_column_->append_datum(DatumArray{"W123", "D4", "A1", "A1", "B2", "Z10"});

    auto activity_array = DatumArray{"A1", "B2", "A1", "C3", "B2", "D4"};

    const auto result = RunConstantMap(activity_array).value();
    ASSERT_EQ(variant_column_->size(), result->size());
    Validate(result, 0, std::vector<int32_t>{0, 1});
    Validate(result, 1, std::vector<int32_t>{0, 1, 0, 1});
    Validate(result, 2, std::vector<int32_t>{0, 0, 1});
    Validate(result, 3, std::vector<int32_t>{0, 0, 1});
    Validate(result, 4, std::nullopt);
    Validate(result, 5, std::vector<int32_t>{-1, 3, 0, 0, 1, -1});
}

TEST_F(CelonisEncodeVariantTest, non_const_map_normal_case) {
    Prepare();

    AddRow(DatumArray{"A12", "B23"}, DatumArray{"A1", "B2", "A12", "B23"});
    AddRow(std::nullopt, DatumArray{"A1", "B2", "A12", "B23"});
    AddRow(DatumArray{"A12", "B23"}, std::nullopt);
    AddRow(std::nullopt, std::nullopt);
    AddRow(DatumArray{"A12", "B23", kNullDatum, "A1", "W2"}, DatumArray{"A1", "B2", "A12", "B23"});
    AddRow(DatumArray{"A12", "B23", kNullDatum, "A1", "W2"}, DatumArray{"A1", "B2", "A12", "B23", "A1"});
    const auto result = Run().value();
    ASSERT_EQ(variant_column_->size(), result->size());

    Validate(result, 0, std::vector<int32_t>{2, 3});
    Validate(result, 1, std::nullopt);
    Validate(result, 2, std::nullopt);
    Validate(result, 3, std::nullopt);
    Validate(result, 4, std::vector<int32_t>{2, 3, 0, -1});
    Validate(result, 5, std::vector<int32_t>{2, 3, 0, -1});
}

} // namespace starrocks
