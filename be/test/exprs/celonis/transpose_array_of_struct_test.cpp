#include "exprs/celonis/transpose_array_of_struct.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "gutil/strings/strcat.h"
#include "testutil/function_utils.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisTransposeArrayOfStructTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    TypeDescriptor to_array_of_struct_type(const std::vector<LogicalType>& logical_types) {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;
        for (int i = 0; i < logical_types.size(); ++i) {
            struct_type.children.emplace_back(logical_types[i]);
            struct_type.field_names.emplace_back(StrCat("col", i));
        }
        TypeDescriptor array_type;
        array_type.type = LogicalType::TYPE_ARRAY;
        array_type.children.emplace_back(struct_type);
        return array_type;
    }

    TypeDescriptor get_return_type(const std::vector<LogicalType>& logical_types) {
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

    std::unique_ptr<FunctionContext> get_ctx(const std::vector<LogicalType>& field_logical_types) {
        std::vector<FunctionContext::TypeDesc> arg_types = {to_array_of_struct_type(field_logical_types)};
        auto return_type = get_return_type(field_logical_types);
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    void Prepare(const std::vector<LogicalType>& field_logical_types) {
        auto array_of_struct_type = to_array_of_struct_type(field_logical_types);
        ctx_ = get_ctx(field_logical_types);
        array_of_struct_column_ = ColumnHelper::create_column(array_of_struct_type, true);
    }

    void AddNullRow() { array_of_struct_column_->append_nulls(1); }

    void AddRow(const std::vector<std::optional<DatumStruct>>& data) {
        DatumArray array;
        for (auto i = 0; i < data.size(); ++i) {
            if (data[i].has_value()) {
                array.emplace_back(data[i].value());
            } else {
                array.emplace_back(kNullDatum);
            }
        }
        array_of_struct_column_->append_datum(array);
    }

    void Equal(const DatumArray& array, const DatumArray& expected_array) {
        ASSERT_EQ(expected_array.size(), array.size());
        for (size_t i = 0; i < expected_array.size(); ++i) {
            EXPECT_EQ(expected_array[i].convert2DatumKey(), array[i].convert2DatumKey());
        }
    }

    void Validate(ColumnPtr& res, size_t row, const vector<DatumArray>& expected) {
        ASSERT_LT(row, res->size());
        StructColumn* st = down_cast<StructColumn*>(ColumnHelper::get_data_column(res.get()));
        auto fields = st->fields_column();
        ASSERT_EQ(expected.size(), fields.size());
        for (auto i = 0; i < expected.size(); ++i) {
            Equal(fields[i]->get(row).get_array(), expected[i]);
        }
    }

    StatusOr<ColumnPtr> Run() {
        StatusOr<ColumnPtr> result;
        result = CelonisTransposeArrayOfStruct::transpose_array_of_struct(ctx_.get(), {array_of_struct_column_});
        return result;
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr array_of_struct_column_;
};

TEST_F(CelonisTransposeArrayOfStructTest, empty_input) {
    Prepare({TYPE_VARCHAR, TYPE_BIGINT});
    const auto result = Run().value();
    ASSERT_TRUE(result->empty());
}

TEST_F(CelonisTransposeArrayOfStructTest, single_field) {
    Prepare({TYPE_VARCHAR});
    DatumStruct ele1 = {Datum{"Apple"}};
    DatumStruct ele2 = {Datum{"Tree"}};
    std::vector<std::optional<DatumStruct>> row1 = std::vector<std::optional<DatumStruct>>({ele1, ele2});
    AddRow(row1);
    DatumStruct ele3 = {Datum{"hello"}};
    DatumStruct ele4 = {Datum{"world"}};
    DatumStruct ele5 = {Datum{""}};
    std::vector<std::optional<DatumStruct>> row2 = std::vector<std::optional<DatumStruct>>({ele3, ele4, ele5});
    AddRow(row2);
    AddNullRow();
    auto result = Run().value();
    ASSERT_EQ(3, result->size());
    Validate(result, 0, {DatumArray{Datum{"Apple"}, Datum{"Tree"}}});
    Validate(result, 1, {DatumArray{Datum{"hello"}, Datum{"world"}, Datum{""}}});
    ASSERT_TRUE(result->is_null(2));
}

TEST_F(CelonisTransposeArrayOfStructTest, two_fields) {
    Prepare({TYPE_VARCHAR, TYPE_BIGINT});
    AddNullRow();
    DatumStruct ele1 = {Datum{"Apple"}, Datum{1L}};
    DatumStruct ele2 = {Datum{"Tree"}, Datum{2L}};
    std::vector<std::optional<DatumStruct>> row1 = std::vector<std::optional<DatumStruct>>({ele1, ele2});
    AddRow(row1);
    AddNullRow();
    DatumStruct ele3 = {Datum{"hello"}, kNullDatum};
    DatumStruct ele4 = {Datum{"world"}, Datum{20L}};
    DatumStruct ele5 = {kNullDatum, Datum{30L}};
    std::vector<std::optional<DatumStruct>> row3 = std::vector<std::optional<DatumStruct>>({ele3, ele4, ele5});
    AddRow(row3);
    auto result = Run().value();
    ASSERT_EQ(4, result->size());
    ASSERT_TRUE(result->is_null(0));
    Validate(result, 1, {DatumArray{Datum{"Apple"}, Datum{"Tree"}}, DatumArray{Datum{1L}, Datum{2L}}});
    ASSERT_TRUE(result->is_null(2));
    Validate(result, 3,
             {DatumArray{Datum{"hello"}, Datum{"world"}, kNullDatum}, DatumArray{kNullDatum, Datum{20L}, Datum{30L}}});
}

TEST_F(CelonisTransposeArrayOfStructTest, all_null_field) {
    Prepare({TYPE_VARCHAR, TYPE_VARCHAR});
    AddNullRow();
    DatumStruct ele1 = {Datum{"Apple"}, kNullDatum};
    DatumStruct ele2 = {Datum{"Tree"}, kNullDatum};
    std::vector<std::optional<DatumStruct>> row1 = std::vector<std::optional<DatumStruct>>({ele1, ele2});
    AddRow(row1);
    AddNullRow();
    DatumStruct ele3 = {Datum{"hello"}, kNullDatum};
    DatumStruct ele4 = {Datum{"world"}, kNullDatum};
    DatumStruct ele5 = {kNullDatum, kNullDatum};
    std::vector<std::optional<DatumStruct>> row3 = std::vector<std::optional<DatumStruct>>({ele3, ele4, ele5});
    AddRow(row3);
    auto result = Run().value();
    ASSERT_EQ(4, result->size());
    ASSERT_TRUE(result->is_null(0));
    Validate(result, 1, {DatumArray{Datum{"Apple"}, Datum{"Tree"}}, DatumArray{kNullDatum, kNullDatum}});
    ASSERT_TRUE(result->is_null(2));
    Validate(result, 3,
             {DatumArray{Datum{"hello"}, Datum{"world"}, kNullDatum}, DatumArray{kNullDatum, kNullDatum, kNullDatum}});
}

TEST_F(CelonisTransposeArrayOfStructTest, three_fields) {
    Prepare({TYPE_VARCHAR, TYPE_BIGINT, TYPE_DOUBLE});
    AddNullRow();
    DatumStruct ele1 = {Datum{"apple"}, Datum{1L}, Datum{1.5}};
    DatumStruct ele2 = {Datum{"pie"}, Datum{-2L}, Datum{-2.5}};
    std::vector<std::optional<DatumStruct>> row2 = std::vector<std::optional<DatumStruct>>({ele1, ele2, std::nullopt});
    AddRow(row2);
    AddNullRow();
    DatumStruct ele3 = {kNullDatum, kNullDatum, kNullDatum};
    DatumStruct ele4 = {Datum{"world"}, Datum{20L}, Datum{20.5}};
    DatumStruct ele5 = {kNullDatum, Datum{30L}, Datum{30.5}};
    std::vector<std::optional<DatumStruct>> row3 =
            std::vector<std::optional<DatumStruct>>({ele3, ele4, std::nullopt, ele5});
    AddRow(row3);
    auto result = Run().value();
    ASSERT_EQ(4, result->size());
    ASSERT_TRUE(result->is_null(0));
    Validate(result, 1,
             {DatumArray{Datum{"apple"}, Datum{"pie"}}, DatumArray{Datum{1L}, Datum{-2L}},
              DatumArray{Datum{1.5}, Datum{-2.5}}});
    ASSERT_TRUE(result->is_null(2));
    Validate(result, 3,
             {DatumArray{kNullDatum, Datum{"world"}, kNullDatum}, DatumArray{kNullDatum, Datum{20L}, Datum{30L}},
              DatumArray{kNullDatum, Datum{20.5}, Datum{30.5}}});
}

} // namespace starrocks
