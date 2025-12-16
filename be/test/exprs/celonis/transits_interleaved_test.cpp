#include "exprs/celonis/transits_interleaved.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/celonis/anyval_util.h"
#include "gutil/strings/strcat.h"
#include "testutil/function_utils.h"
#include "util.h"

namespace starrocks {

class CelonisTransitsInterleavedTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);

private:
    TypeDescriptor logical_type_to_array_type_desc(LogicalType logical_type) {
        if (logical_type == TYPE_VARCHAR) {
            return TYPE_ARRAY_VARCHAR;
        } else if (logical_type == TYPE_DATETIME) {
            return TYPE_ARRAY_DATETIME;
        } else if (logical_type == TYPE_BIGINT) {
            return TYPE_ARRAY_BIGINT;
        } else if (logical_type == TYPE_DOUBLE) {
            return TYPE_ARRAY_DOUBLE;
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

    TypeDescriptor get_return_type(const TypeDescriptor& left_key_struct_type,
                                   const TypeDescriptor& right_key_struct_type) {
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

    std::unique_ptr<FunctionContext> get_ctx(const TypeDescriptor& left_key_struct_type,
                                             const TypeDescriptor& right_key_struct_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                CelonisAnyValUtil::column_type_to_type_desc(left_key_struct_type),
                CelonisAnyValUtil::column_type_to_type_desc(TYPE_ARRAY_DATETIME),
                CelonisAnyValUtil::column_type_to_type_desc(right_key_struct_type),
                CelonisAnyValUtil::column_type_to_type_desc(TYPE_ARRAY_DATETIME),
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN))};
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(
                get_return_type(left_key_struct_type, right_key_struct_type));
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    template <LogicalType LT>
    void Prepare(const std::vector<LogicalType>& left_logical_types,
                 const std::vector<LogicalType>& right_logical_types) {
        auto left_key_struct_type = logical_types_to_struct_type(left_logical_types);
        auto right_key_struct_type = logical_types_to_struct_type(right_logical_types);
        auto array_type_desc = logical_type_to_array_type_desc(LT);
        ctx_ = get_ctx(left_key_struct_type, right_key_struct_type);
        left_primary_keys_column_ = ColumnHelper::create_column(left_key_struct_type, true);
        left_timestamps_column_ = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
        left_sortings_column_ = ColumnHelper::create_column(array_type_desc, true);
        right_primary_keys_column_ = ColumnHelper::create_column(right_key_struct_type, true);
        right_timestamps_column_ = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
        right_sortings_column_ = ColumnHelper::create_column(array_type_desc, true);
        first_last_only_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_BOOLEAN), true);
    }

    void AddRow(const std::optional<std::vector<DatumArray>>& left_keys_arrays,
                const std::optional<DatumArray>& left_timestamps, const std::optional<DatumArray>& left_sortings,
                const std::optional<std::vector<DatumArray>>& right_keys_arrays,
                const std::optional<DatumArray>& right_timestamps, const std::optional<DatumArray>& right_sortings,
                std::optional<bool> first_last_only) {
        auto& left_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(left_primary_keys_column_.get()))
                                    ->fields_column();
        auto left_null_column = down_cast<NullableColumn*>(left_primary_keys_column_.get());
        if (left_keys_arrays.has_value()) {
            left_null_column->null_column_data().emplace_back(0);
            for (auto i = 0; i < left_keys_arrays->size(); ++i) {
                left_fields[i]->append_datum(left_keys_arrays->at(i));
            }
        } else {
            left_primary_keys_column_->append_datum(kNullDatum);
        }
        if (left_timestamps.has_value()) {
            left_timestamps_column_->append_datum(left_timestamps.value());
        } else {
            left_timestamps_column_->append_datum(kNullDatum);
        }
        if (left_sortings.has_value()) {
            left_sortings_column_->append_datum(left_sortings.value());
        } else {
            left_sortings_column_->append_datum(kNullDatum);
        }
        auto& right_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(right_primary_keys_column_.get()))
                                     ->fields_column();
        auto right_null_column = down_cast<NullableColumn*>(right_primary_keys_column_.get());
        if (right_keys_arrays.has_value()) {
            right_null_column->null_column_data().emplace_back(0);
            for (auto i = 0; i < right_keys_arrays->size(); ++i) {
                right_fields[i]->append_datum(right_keys_arrays->at(i));
            }
        } else {
            right_primary_keys_column_->append_datum(kNullDatum);
        }
        if (right_timestamps.has_value()) {
            right_timestamps_column_->append_datum(right_timestamps.value());
        } else {
            right_timestamps_column_->append_datum(kNullDatum);
        }
        if (right_sortings.has_value()) {
            right_sortings_column_->append_datum(right_sortings.value());
        } else {
            right_sortings_column_->append_datum(kNullDatum);
        }
        if (first_last_only.has_value()) {
            first_last_only_column_->append_datum(first_last_only.value());
        } else {
            first_last_only_column_->append_datum(kNullDatum);
        }
    }

    void Equal(const DatumArray& array, const DatumArray& expected_array) {
        ASSERT_EQ(expected_array.size(), array.size());
        for (size_t i = 0; i < expected_array.size(); ++i) {
            EXPECT_EQ(expected_array[i].convert2DatumKey(), array[i].convert2DatumKey());
        }
    }

    void Validate(ColumnPtr& res, size_t row, const std::vector<DatumArray>& expected_left_arrays,
                  const std::vector<DatumArray>& expected_right_arrays) {
        ASSERT_LT(row, res->size());
        StructColumn* st = down_cast<StructColumn*>(ColumnHelper::get_data_column(res.get()));
        auto fields = st->fields_column();
        ASSERT_EQ(2, fields.size());
        const StructColumn* res_left_column =
                down_cast<const StructColumn*>(ColumnHelper::get_data_column(fields[0].get()));
        const StructColumn* res_right_column =
                down_cast<const StructColumn*>(ColumnHelper::get_data_column(fields[1].get()));
        auto res_left_fields = res_left_column->fields();
        auto res_right_fields = res_right_column->fields();
        ASSERT_EQ(res_left_fields.size(), expected_left_arrays.size());
        ASSERT_EQ(res_right_fields.size(), expected_right_arrays.size());
        for (auto i = 0; i < expected_left_arrays.size(); ++i) {
            Equal(expected_left_arrays[i], res_left_fields[i]->get(row).get_array());
        }
        for (auto i = 0; i < expected_right_arrays.size(); ++i) {
            Equal(expected_right_arrays[i], res_right_fields[i]->get(row).get_array());
        }
    }

    StatusOr<ColumnPtr> Run() {
        StatusOr<ColumnPtr> result;
        result = CelonisTransitsInterleaved::transits_interleaved(
                ctx_.get(),
                {left_primary_keys_column_, left_timestamps_column_, left_sortings_column_, right_primary_keys_column_,
                 right_timestamps_column_, right_sortings_column_, first_last_only_column_});
        return result;
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr left_primary_keys_column_;
    ColumnPtr left_timestamps_column_;
    ColumnPtr left_sortings_column_;
    ColumnPtr right_primary_keys_column_;
    ColumnPtr right_timestamps_column_;
    ColumnPtr right_sortings_column_;
    ColumnPtr first_last_only_column_;
};

TEST_F(CelonisTransitsInterleavedTest, empty_input) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    const auto result = Run().value();
    ASSERT_TRUE(result->empty());
}

TEST_F(CelonisTransitsInterleavedTest, null_column_input) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(std::nullopt, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt, std::nullopt,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, std::nullopt, std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               right_keys_arrays, std::nullopt, std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               right_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, std::nullopt);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsInterleavedTest, different_key_length) {
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1"}}, {DatumArray{"R1"}});
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0), TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               std::nullopt, right_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 7, 0, 0, 0)}, std::nullopt,
               false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L2"}}, {DatumArray{"R1"}});
    }
}

TEST_F(CelonisTransitsInterleavedTest, inconsistent_keys_length) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{"L1"}, DatumArray{1L, 2L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2"}, DatumArray{2L, 3L}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{"L1", "L2"}, DatumArray{1L, 2L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2"}, DatumArray{2L}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0), TimestampValue::create(1970, 1, 8, 0, 0, 0)},
               std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsInterleavedTest, null_timestamp) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0), kNullDatum}, std::nullopt,
               right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               std::nullopt, right_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0), kNullDatum},
               std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsInterleavedTest, wrong_timestamps_length) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               std::nullopt, right_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt,
               false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0), TimestampValue::create(1970, 1, 7, 0, 0, 0)},
               std::nullopt, false);
        const auto result = Run().value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisTransitsInterleavedTest, empty_keys) {
    Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{}};
    AddRow(left_keys_arrays, DatumArray{}, DatumArray{}, right_keys_arrays, DatumArray{}, DatumArray{}, false);
    auto result = Run().value();
    ASSERT_EQ(1, result->size());
    Validate(result, 0, {DatumArray{}}, {DatumArray{}});
}

TEST_F(CelonisTransitsInterleavedTest, different_left_key_type_and_right_key_type) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{1L, 2L, 3L}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 5, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                          TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2", "L3", "L3"}}, {DatumArray{1L, 1L, 2L, 2L, 3L}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_BIGINT}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{1L, 2L, 3L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 5, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                          TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{1L, 2L, 2L, 3L, 3L}}, {DatumArray{"R1", "R1", "R2", "R2", "R3"}});
    }
}

TEST_F(CelonisTransitsInterleavedTest, number_of_left_fields_different_from_right_fields) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}, DatumArray{1L, 2L, 3L}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 5, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                          TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2", "L3", "L3"}},
                 {DatumArray{"R1", "R1", "R2", "R2", "R3"}, DatumArray{1L, 1L, 2L, 2L, 3L}});
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_BIGINT, TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{1L, 2L, 3L}, DatumArray{"L11", "L22", "L33"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 5, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                          TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{1L, 2L, 2L, 3L, 3L}, DatumArray{"L11", "L22", "L22", "L33", "L33"}},
                 {DatumArray{"R1", "R1", "R2", "R2", "R3"}});
    }
}

TEST_F(CelonisTransitsInterleavedTest, null_left_sorting_element) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"1", kNullDatum, "5"}, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"2", "4", "6"}, false);
    auto result = Run().value();
    ASSERT_EQ(1, result->size());
    EXPECT_TRUE(result->get(0).is_null());
}

TEST_F(CelonisTransitsInterleavedTest, left_sortings_inconsistent_with_left_timestamps) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"1", "3", "5", "7"}, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"2", "4", "6"}, false);
    auto result = Run();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(),
              "If provided, the size of left_sortings_array and left_timestamps_array should not be different.");
}

TEST_F(CelonisTransitsInterleavedTest, null_right_sorting_element) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"1", "2", "5"}, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"2", "4", kNullDatum}, false);
    auto result = Run().value();
    ASSERT_EQ(1, result->size());
    EXPECT_TRUE(result->get(0).is_null());
}

TEST_F(CelonisTransitsInterleavedTest, right_sortings_inconsistent_with_right_timestamps) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"1", "3", "5"}, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{"2", "4"}, false);
    auto result = Run();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(),
              "If provided, the size of right_sortings_array and right_timestamps_array should not be different.");
}

TEST_F(CelonisTransitsInterleavedTest, first_last_only_with_sortings) {
    Prepare<TYPE_DOUBLE>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{1.5, 3.5, 5.5}, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{2.5, 4.5, 6.5}, true);
    auto result = Run().value();
    ASSERT_EQ(1, result->size());
    Validate(result, 0, {DatumArray{"L1", "L3"}}, {DatumArray{"R1", "R2"}});
}

TEST_F(CelonisTransitsInterleavedTest, empty_right) {
    Prepare<TYPE_DOUBLE>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{}};
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{1.5, 3.5, 5.5}, right_keys_arrays, DatumArray{}, DatumArray{}, true);
    auto result = Run().value();
    ASSERT_EQ(1, result->size());
    Validate(result, 0, {DatumArray{}}, {DatumArray{}});
}

TEST_F(CelonisTransitsInterleavedTest, empty_left) {
    Prepare<TYPE_DOUBLE>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
    AddRow(left_keys_arrays, DatumArray{}, DatumArray{}, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                      TimestampValue::create(1970, 1, 1, 0, 0, 0)},
           DatumArray{2.5, 4.5, 6.5}, true);
    auto result = Run().value();
    ASSERT_EQ(1, result->size());
    Validate(result, 0, {DatumArray{}}, {DatumArray{}});
}

TEST_F(CelonisTransitsInterleavedTest, normal_cases_with_sortings) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                          TimestampValue::create(1970, 1, 1, 0, 0, 0)},
               DatumArray{"1", "3", "5"}, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 0),
                          TimestampValue::create(1970, 1, 1, 0, 0, 0)},
               DatumArray{"2", "4", "6"}, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2", "L3", "L3"}}, {DatumArray{"R1", "R1", "R2", "R2", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 5, 0, 0, 0)},
               DatumArray{"5", "3", "1"}, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                          TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               DatumArray{"6", "4", "2"}, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2", "L3", "L3"}}, {DatumArray{"R1", "R1", "R2", "R2", "R3"}});
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR, TYPE_VARCHAR}, {TYPE_VARCHAR, TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{"L1", "L2"}, DatumArray{"W", "X"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2"}, DatumArray{"Y", "Z"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0)},
               DatumArray{1L, 0L}, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0)},
               DatumArray{2L, 0L}, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2"}, DatumArray{"W", "X", "X"}},
                 {DatumArray{"R1", "R1", "R2"}, DatumArray{"Y", "Y", "Z"}});
    }
}

TEST_F(CelonisTransitsInterleavedTest, normal_cases) {
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 5, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                          TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2", "L3", "L3"}}, {DatumArray{"R1", "R1", "R2", "R2", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 4, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 3, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 3, 0, 0, 0)},
               std::nullopt, true);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L2", "L3"}}, {DatumArray{"R1", "R3"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_VARCHAR}, {TYPE_VARCHAR, TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{"L1", "L2"}, DatumArray{"W", "X"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2"}, DatumArray{"Y", "Z"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2"}, DatumArray{"W", "X", "X"}},
                 {DatumArray{"R1", "R1", "R2"}, DatumArray{"Y", "Y", "Z"}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_VARCHAR}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{"L1", "L2"}, DatumArray{"W", "X"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2"}, DatumArray{1L, 2L}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2"}, DatumArray{"W", "X", "X"}},
                 {DatumArray{"R1", "R1", "R2"}, DatumArray{1L, 1L, 2L}});
    }
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR, TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{"L1", "L2"}, DatumArray{1L, 2L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2"}, DatumArray{3L, 4L}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2"}, DatumArray{1L, 2L, 2L}},
                 {DatumArray{"R1", "R1", "R2"}, DatumArray{3L, 3L, 4L}});
    }
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR, TYPE_BIGINT}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays =
                std::vector<DatumArray>{DatumArray{"L1", "L2"}, DatumArray{1L, 2L}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0)},
               std::nullopt, false);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L2", "L2"}, DatumArray{1L, 2L, 2L}}, {DatumArray{"R1", "R1", "R2"}});
    }
}

TEST_F(CelonisTransitsInterleavedTest, first_last_only) {
    // result array length = 0
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{}};
        AddRow(left_keys_arrays, DatumArray{}, DatumArray{}, right_keys_arrays, DatumArray{}, DatumArray{}, true);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{}}, {DatumArray{}});
    }
    // result array length = 1
    {
        Prepare<TYPE_BIGINT>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1"}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               right_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, true);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1"}}, {DatumArray{"R1"}});
    }
    // result array length = 1
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_BIGINT});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{2L}};
        AddRow(left_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 5, 0, 0, 0)}, std::nullopt,
               right_keys_arrays, DatumArray{TimestampValue::create(1970, 1, 6, 0, 0, 0)}, std::nullopt, true);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1"}}, {DatumArray{2L}});
    }
    // result array length = 2
    {
        Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
        std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
        std::optional<std::vector<DatumArray>> right_keys_arrays =
                std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
        AddRow(left_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                          TimestampValue::create(1970, 1, 5, 0, 0, 0)},
               std::nullopt, right_keys_arrays,
               DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                          TimestampValue::create(1970, 1, 6, 0, 0, 0)},
               std::nullopt, true);
        auto result = Run().value();
        ASSERT_EQ(1, result->size());
        Validate(result, 0, {DatumArray{"L1", "L3"}}, {DatumArray{"R1", "R2"}});
    }
}

TEST_F(CelonisTransitsInterleavedTest, multiple_rows) {
    Prepare<TYPE_VARCHAR>({TYPE_VARCHAR}, {TYPE_VARCHAR});
    std::optional<std::vector<DatumArray>> left_keys_arrays = std::vector<DatumArray>{DatumArray{"L1", "L2", "L3"}};
    std::optional<std::vector<DatumArray>> right_keys_arrays = std::vector<DatumArray>{DatumArray{"R1", "R2", "R3"}};
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                      TimestampValue::create(1970, 1, 5, 0, 0, 0)},
           std::nullopt, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                      TimestampValue::create(1970, 1, 6, 0, 0, 0)},
           std::nullopt, false);
    AddRow(left_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0), TimestampValue::create(1970, 1, 4, 0, 0, 0),
                      TimestampValue::create(1970, 1, 6, 0, 0, 0)},
           std::nullopt, right_keys_arrays,
           DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 3, 0, 0, 0),
                      TimestampValue::create(1970, 1, 5, 0, 0, 0)},
           std::nullopt, false);
    auto result = Run().value();
    ASSERT_EQ(2, result->size());
    Validate(result, 0, {DatumArray{"L1", "L2", "L2", "L3", "L3"}}, {DatumArray{"R1", "R1", "R2", "R2", "R3"}});
    Validate(result, 1, {DatumArray{"L1", "L1", "L2", "L2", "L3"}}, {DatumArray{"R1", "R2", "R2", "R3", "R3"}});
}

} // namespace starrocks
