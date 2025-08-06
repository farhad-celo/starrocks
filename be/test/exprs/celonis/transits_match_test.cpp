#include "exprs/celonis/transits_match.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "gutil/strings/strcat.h"
#include "testutil/function_utils.h"
#include "util.h"
#include "util/defer_op.h"

#include <gtest/gtest.h>

namespace starrocks {

class CelonisTransitsMatchTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);

private:

    TypeDescriptor logical_type_to_array_type_desc(LogicalType logical_type) {
        if (logical_type == TYPE_VARCHAR) {
            return TYPE_ARRAY_VARCHAR;
        } else if (logical_type == TYPE_DATETIME) {
            return TYPE_ARRAY_DATETIME;
        } else if (logical_type == TYPE_BIGINT) {
            return TYPE_ARRAY_BIGINT;
        }
        return TYPE_ARRAY_VARCHAR;
    }

    TypeDescriptor logical_types_to_struct_type(const std::vector<LogicalType>& logical_types) {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;
        for (int i = 0; i < logical_types.size(); ++i) {
            TypeDescriptor array_type;
            array_type.type = LogicalType::TYPE_ARRAY;
            array_type.children.emplace_back(logical_types[i]);
            struct_type.children.emplace_back(array_type);
            struct_type.field_names.emplace_back(StrCat("col", i));
        }
        return struct_type;
    }

    TypeDescriptor
    get_return_type(const TypeDescriptor& left_key_struct_type, const TypeDescriptor& right_key_struct_type) {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;

        struct_type.children.emplace_back(LogicalType::TYPE_STRUCT);
        struct_type.field_names.emplace_back("left");
        struct_type.children.emplace_back(LogicalType::TYPE_STRUCT);
        struct_type.field_names.emplace_back("right");

        for (auto i = 0; i < left_key_struct_type.children.size(); ++i) {
            struct_type.children[0].field_names.emplace_back(StrCat("Col ", left_key_struct_type.field_names[i]));
            struct_type.children[0].children.emplace_back(left_key_struct_type.children[i]);
        }
        for (auto i = 0; i < right_key_struct_type.children.size(); ++i) {
            struct_type.children[1].field_names.emplace_back(StrCat("Col ", right_key_struct_type.field_names[i]));
            struct_type.children[1].children.emplace_back(right_key_struct_type.children[i]);
        }
        return struct_type;
    }

    std::unique_ptr<FunctionContext>
    get_ctx(const TypeDescriptor& left_key_struct_type, const TypeDescriptor& right_key_struct_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(left_key_struct_type),
                AnyValUtil::column_type_to_type_desc(TYPE_ARRAY_VARCHAR),
                AnyValUtil::column_type_to_type_desc(right_key_struct_type),
                AnyValUtil::column_type_to_type_desc(TYPE_ARRAY_VARCHAR),
                AnyValUtil::column_type_to_type_desc(TYPE_ARRAY_VARCHAR),
                AnyValUtil::column_type_to_type_desc(TYPE_ARRAY_VARCHAR)};
        auto return_type = AnyValUtil::column_type_to_type_desc(
                get_return_type(left_key_struct_type, right_key_struct_type));
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    template<LogicalType LT>
    void
    Prepare(const std::vector<LogicalType>& left_logical_types, const std::vector<LogicalType>& right_logical_types) {
        auto left_key_struct_type = logical_types_to_struct_type(left_logical_types);
        auto right_key_struct_type = logical_types_to_struct_type(right_logical_types);
        ctx_ = get_ctx(left_key_struct_type, right_key_struct_type);
        auto array_type_desc = logical_type_to_array_type_desc(LT);
        left_primary_keys_column_ = ColumnHelper::create_column(left_key_struct_type, true);
        left_match_column_ = ColumnHelper::create_column(array_type_desc, true);
        right_primary_keys_column_ = ColumnHelper::create_column(right_key_struct_type, true);
        right_match_column_ = ColumnHelper::create_column(array_type_desc, true);
        left_manual_column_ = ColumnHelper::create_column(array_type_desc, true);
        right_manual_column_ = ColumnHelper::create_column(array_type_desc, true);
    }

    void AddRow(const std::optional<std::vector<DatumArray>>& left_keys_arrays,
                const std::optional<DatumArray>& left_match,
                const std::optional<std::vector<DatumArray>>& right_keys_arrays,
                const std::optional<DatumArray>& right_match) {
        auto& left_fields = down_cast<StructColumn*>(
                ColumnHelper::get_data_column(left_primary_keys_column_.get()))->fields_column();
        auto left_null_column = down_cast<NullableColumn*>(left_primary_keys_column_.get());
        if (left_keys_arrays.has_value()) {
            left_null_column->null_column_data().emplace_back(0);
            for (auto i = 0; i < left_keys_arrays->size(); ++i) {
                left_fields[i]->append_datum(left_keys_arrays->at(i));
            }
        } else {
            left_primary_keys_column_->append_datum(kNullDatum);
        }
        if (left_match.has_value()) {
            left_match_column_->append_datum(left_match.value());
        } else {
            left_match_column_->append_datum(kNullDatum);
        }
        auto& right_fields = down_cast<StructColumn*>(
                ColumnHelper::get_data_column(right_primary_keys_column_.get()))->fields_column();
        auto right_null_column = down_cast<NullableColumn*>(right_primary_keys_column_.get());
        if (right_keys_arrays.has_value()) {
            right_null_column->null_column_data().emplace_back(0);
            for (auto i = 0; i < right_keys_arrays->size(); ++i) {
                right_fields[i]->append_datum(right_keys_arrays->at(i));
            }
        } else {
            right_primary_keys_column_->append_datum(kNullDatum);
        }
        if (right_match.has_value()) {
            right_match_column_->append_datum(right_match.value());
        } else {
            right_match_column_->append_datum(kNullDatum);
        }
    }

    void AddRow(const std::optional<std::vector<DatumArray>>& left_keys_arrays,
                const std::optional<DatumArray>& left_match,
                const std::optional<std::vector<DatumArray>>& right_keys_arrays,
                const std::optional<DatumArray>& right_match,
                const std::optional<DatumArray>& left_manual,
                const std::optional<DatumArray>& right_manual) {
        AddRow(left_keys_arrays, left_match, right_keys_arrays, right_match);
        if (left_manual.has_value()) {
            left_manual_column_->append_datum(left_manual.value());
        } else {
            left_manual_column_->append_datum(kNullDatum);
        }
        if (right_manual.has_value()) {
            right_manual_column_->append_datum(right_manual.value());
        } else {
            right_manual_column_->append_datum(kNullDatum);
        }
    }

    void Equal(const DatumArray& array, const DatumArray& expected_array) {
        ASSERT_EQ(expected_array.size(), array.size());
        for (size_t i = 0; i < expected_array.size(); ++i) {
            EXPECT_EQ(expected_array[i].convert2DatumKey(), array[i].convert2DatumKey());
        }
    }

    void Validate(const ColumnPtr& res, size_t row, const std::vector<DatumArray>& expected_left_arrays,
                  const std::vector<DatumArray>& expected_right_arrays) {
        ASSERT_LT(row, res->size());
        StructColumn* st = down_cast<StructColumn*>(ColumnHelper::get_data_column(res.get()));
        auto fields = st->fields_column();
        ASSERT_EQ(2, fields.size());
        const StructColumn* res_left_column = down_cast<const StructColumn*>(ColumnHelper::get_data_column(fields[0].get()));
        const StructColumn* res_right_column = down_cast<const StructColumn*>(ColumnHelper::get_data_column(fields[1].get()));
        auto res_left_fields = res_left_column->fields();
        auto res_right_fields = res_right_column->fields();
        ASSERT_EQ(res_left_fields.size(), expected_left_arrays.size());
        ASSERT_EQ(res_right_fields.size(), expected_right_arrays.size());
        for (auto i = 0; i < expected_left_arrays.size(); ++i) {
            Equal(res_left_fields[i]->get(row).get_array(), expected_left_arrays[i]);
        }
        for (auto i = 0; i < expected_right_arrays.size(); ++i) {
            Equal(res_right_fields[i]->get(row).get_array(), expected_right_arrays[i]);
        }
    }

    StatusOr<ColumnPtr>
    RunConstantManual(const std::optional<DatumArray>& left_manual, const std::optional<DatumArray>& right_manual) {
        if (left_manual.has_value()) {
            left_manual_column_->append_datum(left_manual.value());
        } else {
            left_manual_column_->append_datum(kNullDatum);
        }
        if (right_manual.has_value()) {
            right_manual_column_->append_datum(right_manual.value());
        } else {
            right_manual_column_->append_datum(kNullDatum);
        }
        const auto size = left_primary_keys_column_->size();
        left_manual_column_ = ConstColumn::create(left_manual_column_, size);
        right_manual_column_ = ConstColumn::create(right_manual_column_, size);
        ctx_->set_constant_columns({nullptr, nullptr, nullptr, nullptr, left_manual_column_, right_manual_column_});
        return Run();
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisTransitsMatch::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisTransitsMatch::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisTransitsMatch::close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisTransitsMatch::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        Columns columns;
        columns.push_back(left_primary_keys_column_);
        columns.push_back(left_match_column_);
        columns.push_back(right_primary_keys_column_);
        columns.push_back(right_match_column_);
        columns.push_back(left_manual_column_);
        columns.push_back(right_manual_column_);
        result = CelonisTransitsMatch::transits_match(ctx_.get(), columns);
        return result;
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr left_primary_keys_column_;
    ColumnPtr left_match_column_;
    ColumnPtr right_primary_keys_column_;
    ColumnPtr right_match_column_;
    ColumnPtr left_manual_column_;
    ColumnPtr right_manual_column_;

};

TEST_F(CelonisTransitsMatchTest, empty_input) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    const auto result = Run().value();
    ASSERT_TRUE(result->empty());
}

TEST_F(CelonisTransitsMatchTest, null_column_input) {
    {
        Prepare<TYPE_DATETIME>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(std::nullopt,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_DATETIME>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_DATETIME>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays,
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_DATETIME>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, right_keys_arrays,
               std::nullopt, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsMatchTest, inconsistent_left_and_right_manual) {
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{1L}, right_keys_arrays, DatumArray{2L}, std::nullopt, DatumArray{1L});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{1L}, right_keys_arrays, DatumArray{2L}, DatumArray{1L}, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{1L}, right_keys_arrays, DatumArray{2L});
        const auto result = RunConstantManual(std::nullopt, DatumArray{1L}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{1L}, right_keys_arrays, DatumArray{2L});
        const auto result = RunConstantManual(DatumArray{1L}, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsMatchTest, different_key_length) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(!result->get(0).is_null());
        Validate(result, 0, {DatumArray{"L1", "L1"}}, {DatumArray{"R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(!result->get(0).is_null());
        Validate(result, 0, {DatumArray{"L1", "L1"}}, {DatumArray{"R1", "R3"}});
    }
}

TEST_F(CelonisTransitsMatchTest, inconsistent_keys_length) {
    {
        Prepare<TYPE_DATETIME>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"},
                                                                                          DatumArray{1L, 2L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"},
                                                                                           DatumArray{2L, 3L}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0),
                          TimestampValue::create(1970, 1, 7, 0, 0, 0)}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_DATETIME>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"},
                                                                                          DatumArray{1L, 2L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"},
                                                                                           DatumArray{2L}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L3"}}, {DatumArray{"R1", "R1"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo"}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L3"}}, {DatumArray{"R1", "R1"}});
    }
}

TEST_F(CelonisTransitsMatchTest, null_match) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", kNullDatum}, right_keys_arrays, DatumArray{"foo", "bar"},
               std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{kNullDatum, "bar"},
               std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsMatchTest, left_right_manual_length_mismatch) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{"foo", "bar"},
               DatumArray{"foo"}, DatumArray{"far", "bar"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{"foo", "bar"});
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{"far", "baz"});
        const auto result = RunConstantManual(DatumArray{"foo"}, DatumArray{"far", "bar"}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisTransitsMatchTest, null_manual) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{"foo", "bar"},
               DatumArray{kNullDatum}, DatumArray{"far"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{"tar", "bar"},
               DatumArray{"far"}, DatumArray{kNullDatum});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{"foo", "bar"});
        const auto result = RunConstantManual(DatumArray{kNullDatum}, DatumArray{"far"}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar"}, right_keys_arrays, DatumArray{"foo", "bar"});
        const auto result = RunConstantManual(DatumArray{"far"}, DatumArray{kNullDatum}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsMatchTest, wrong_match_length) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"L1", "L2"}, right_keys_arrays, DatumArray{"L3"}, std::nullopt,
               std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{"L1"}, right_keys_arrays, DatumArray{"L3", "L4"}, std::nullopt,
               std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsMatchTest, empty_keys) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{}};
        AddRow(left_keys_arrays, DatumArray{}, right_keys_arrays, DatumArray{}, std::nullopt,
               std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{}}, {DatumArray{}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{}};
        AddRow(left_keys_arrays, DatumArray{}, right_keys_arrays, DatumArray{}, DatumArray{}, DatumArray{});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{}}, {DatumArray{}});
    }
}

TEST_F(CelonisTransitsMatchTest, number_of_left_fields_different_from_right_fields) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_BIGINT, TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{1L, 2L, 3L}, DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{1L, 1L, 3L, 3L}, DatumArray{"L1", "L1", "L3", "L3"}},
                 {DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}, DatumArray{1L, 2L, 3L}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}},
                 {DatumArray{"R1", "R3", "R1", "R3"}, DatumArray{1L, 3L, 1L, 3L}});
    }
}

TEST_F(CelonisTransitsMatchTest, different_left_key_type_and_right_key_type) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_BIGINT}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{1L, 2L, 3L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{1L, 1L, 3L, 3L}}, {DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{1L, 2L, 3L}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}}, {DatumArray{1L, 3L, 1L, 3L}});
    }
}

TEST_F(CelonisTransitsMatchTest, normal_cases_const_manual) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}}, {DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays1 = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays1 = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays1, DatumArray{"foo", "bar", "foo"}, right_keys_arrays1,
               DatumArray{"foo", "baz", "foo"});
        std::optional<std::vector<DatumArray>> left_keys_arrays2 = std::vector<DatumArray>{
                DatumArray{"LL1", "LL2", "LL3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays2 = std::vector<DatumArray>{
                DatumArray{"RR1", "RR2", "RR3"}};
        AddRow(left_keys_arrays2, DatumArray{"foo", "bar", "foo"}, right_keys_arrays2,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(std::nullopt, std::nullopt).value();
        ASSERT_EQ(2, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}}, {DatumArray{"R1", "R3", "R1", "R3"}});
        Validate(result, 1, {DatumArray{"LL1", "LL1", "LL3", "LL3"}}, {DatumArray{"RR1", "RR3", "RR1", "RR3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"});
        const auto result = RunConstantManual(DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"}).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}}, {DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"}, DatumArray{}, DatumArray{});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{}}, {DatumArray{}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"});
        const auto result = RunConstantManual(DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"}).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{}}, {DatumArray{}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}, {1L, 2L, 3L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}, {11L, 12L, 13L}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"});
        const auto result = RunConstantManual(DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"}).value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}, DatumArray{1L, 1L, 3L, 3L}},
                 {DatumArray{"R1", "R3", "R1", "R3"}, DatumArray{11L, 13L, 11L, 13L}});
    }
}

TEST_F(CelonisTransitsMatchTest, normal_cases) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}}, {DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"}, DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}}, {DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}, DatumArray{11L, 22L, 33L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"}, DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}, DatumArray{11L, 11L, 33L, 33L}},
                 {DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_BIGINT, TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{11L, 22L, 33L},
                                                                                           DatumArray{"R1", "R2",
                                                                                                      "R3"}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"}, DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}},
                 {DatumArray{11L, 33L, 11L, 33L}, DatumArray{"R1", "R3", "R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"}, DatumArray{}, DatumArray{});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{}}, {DatumArray{}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays, DatumArray{"foo", "bar", "foo"}, right_keys_arrays,
               DatumArray{"foo", "baz", "foo"}, DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{}}, {DatumArray{}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}, {1L, 2L, 3L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{"R1", "R2", "R3"}, {11L, 12L, 13L}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"}, DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}, DatumArray{1L, 1L, 3L, 3L}},
                 {DatumArray{"R1", "R3", "R1", "R3"}, DatumArray{11L, 13L, 11L, 13L}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_BIGINT, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{
                DatumArray{"L1", "L2", "L3"}, {1L, 2L, 3L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{
                DatumArray{1L, 2L, 3L}, {11L, 12L, 13L}};
        AddRow(left_keys_arrays, DatumArray{"a", "bar", "b"}, right_keys_arrays,
               DatumArray{"c", "baz", "d"}, DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"});
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}, DatumArray{1L, 1L, 3L, 3L}},
                 {DatumArray{1L, 3L, 1L, 3L}, DatumArray{11L, 13L, 11L, 13L}});
    }
}

TEST_F(CelonisTransitsMatchTest, multiple_rows) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays1 = std::vector<DatumArray>{
            DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays1 = std::vector<DatumArray>{
            DatumArray{"R1", "R2", "R3"}};
    std::optional<std::vector<DatumArray>> left_keys_arrays2 = std::vector<DatumArray>{
            DatumArray{"LL1", "LL2", "LL3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays2 = std::vector<DatumArray>{
            DatumArray{"RR1", "RR2", "RR3"}};
    AddRow(left_keys_arrays1, DatumArray{"foo", "bar", "foo"}, right_keys_arrays1,
           DatumArray{"foo", "baz", "foo"}, std::nullopt, std::nullopt);
    AddRow(left_keys_arrays2, DatumArray{"a", "bar", "b"}, right_keys_arrays2,
           DatumArray{"c", "baz", "d"}, DatumArray{"a", "a", "b", "b"}, std::nullopt);
    AddRow(left_keys_arrays2, DatumArray{"a", "bar", "b"}, right_keys_arrays2,
           DatumArray{"c", "baz", "d"}, DatumArray{"a", "a", "b", "b"}, DatumArray{"c", "d", "c", "d"});
    const auto result = Run().value();
    ASSERT_EQ(3, result->size());
    Validate(result, 0, {DatumArray{"L1", "L1", "L3", "L3"}}, {DatumArray{"R1", "R3", "R1", "R3"}});
    EXPECT_TRUE(result->get(1).is_null());
    Validate(result, 2, {DatumArray{"LL1", "LL1", "LL3", "LL3"}}, {DatumArray{"RR1", "RR3", "RR1", "RR3"}});
}

} // namespace starrocks
