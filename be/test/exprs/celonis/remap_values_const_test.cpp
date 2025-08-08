#include "exprs/celonis/remap_values.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

#include <gtest/gtest.h>

namespace starrocks {

class CelonisRemapValuesConstTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    template<LogicalType LT>
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(LT),
                TypeDescriptor::from_logical_type(TYPE_ARRAY),
                TypeDescriptor::from_logical_type(TYPE_ARRAY),
                TypeDescriptor::from_logical_type(LT)};
        auto return_type = TypeDescriptor::from_logical_type(LT);
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        value_column_ = ColumnHelper::create_column(TypeDescriptor(LT), true);
        default_column_ = ColumnHelper::create_column(TypeDescriptor(LT), true);
        // Array Literal is not wrapped with ConstColumn.
        // As of 2024-01-30, it has one row in FunctionContext::constant_column_ and it is evaluated and unfolded to
        // multiple rows in /be/src/exprs/array_expr.cpp before it is passed to celonis_in().
        // In this test, we don't unfold the column when we call the function as the function doesn't read it.
        old_array_column_ = ColumnHelper::create_column(celonis::array_type(LT), false);
        new_array_column_ = ColumnHelper::create_column(celonis::array_type(LT), false);
    }

    void
    AddRow(const Datum& value, const DatumArray& old_array, const DatumArray& new_array, const Datum& default_value) {
        value_column_->append_datum(value);
        old_array_column_->append_datum(old_array);
        new_array_column_->append_datum(new_array);
        default_column_->append_datum(default_value);
    }

    template<LogicalType LT>
    StatusOr<ColumnPtr> Run(bool has_default) {
        DeferOp close_fragment_local([this] {
            CelonisRemapValues<LT>::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisRemapValues<LT>::prepare_const(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisRemapValues<LT>::close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisRemapValues<LT>::prepare_const(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        if (has_default) {
            result = CelonisRemapValues<LT>::remap_values_const(ctx_.get(),
                                                                {value_column_, old_array_column_, new_array_column_,
                                                                 default_column_});
        } else {
            result = CelonisRemapValues<LT>::remap_values_const(ctx_.get(),
                                                                {value_column_, old_array_column_, new_array_column_});
        }
        return result;
    }

    template<LogicalType LT>
    StatusOr<ColumnPtr>
    RunConstantValueMap(const DatumArray& old_array, const DatumArray& new_array, bool has_default) {
        old_array_column_->append_datum(old_array);
        new_array_column_->append_datum(new_array);
        const auto nrows = value_column_->size();
        old_array_column_ = ConstColumn::create(old_array_column_, nrows);
        new_array_column_ = ConstColumn::create(new_array_column_, nrows);
        ctx_->set_constant_columns({nullptr, old_array_column_, new_array_column_, nullptr});
        return Run<LT>(has_default);
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr value_column_;
    ColumnPtr old_array_column_;
    ColumnPtr new_array_column_;
    ColumnPtr default_column_;
};

TEST_F(CelonisRemapValuesConstTest, const_inconsist_value_map) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    default_column_->append_datum(kNullDatum);

    auto old_array = DatumArray{"string1", "string2"};
    auto new_array = DatumArray{"string1-new", "string2-new", "string3-new"};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true);
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("[prepare] old value array must have the same length as new value array.",
              result.status().message());
}

TEST_F(CelonisRemapValuesConstTest, non_const_inconsist_value_map) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    AddRow("string", DatumArray{"string1", "string2"}, DatumArray{"string2"}, "default");

    const auto result = Run<LT>(true);
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("old value array must have the same length as new value array.",
              result.status().message());
}

TEST_F(CelonisRemapValuesConstTest, empty_value_column) {
    {
        const LogicalType LT = TYPE_VARCHAR;
        Prepare<LT>();
        const auto result = Run<LT>(true).value();
        ASSERT_EQ(0, result->size());
    }
    {
        const LogicalType LT = TYPE_VARCHAR;
        Prepare<LT>();
        const auto result = Run<LT>(false).value();
        ASSERT_EQ(0, result->size());
    }
    {
        const LogicalType LT = TYPE_BIGINT;
        Prepare<LT>();
        const auto result = Run<LT>(true).value();
        ASSERT_EQ(0, result->size());
    }
    {
        const LogicalType LT = TYPE_BIGINT;
        Prepare<LT>();
        const auto result = Run<LT>(false).value();
        ASSERT_EQ(0, result->size());
    }
}

TEST_F(CelonisRemapValuesConstTest, null_in_value_map_with_default) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(kNullDatum);
    value_column_->append_datum("string4");

    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum("default");

    auto old_array = DatumArray{"string1", kNullDatum, "string2"};
    auto new_array = DatumArray{"string1-new", "NULL", "string3"};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
    ASSERT_EQ(4, result->size());
    EXPECT_EQ("string1-new", result->get(0).get_slice());
    EXPECT_EQ("string3", result->get(1).get_slice());
    EXPECT_EQ("NULL", result->get(2).get_slice());
    EXPECT_EQ("default", result->get(3).get_slice());
}

TEST_F(CelonisRemapValuesConstTest, null_in_value_map_without_default) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(kNullDatum);
    value_column_->append_datum("string4");

    auto old_array = DatumArray{"string1", kNullDatum, "string2", "string2"};
    auto new_array = DatumArray{"string1-new", "NULL", "string3", kNullDatum};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, false).value();
    ASSERT_EQ(4, result->size());
    EXPECT_EQ("string1-new", result->get(0).get_slice());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ("NULL", result->get(2).get_slice());
    EXPECT_EQ("string4", result->get(3).get_slice());
}

TEST_F(CelonisRemapValuesConstTest, string_with_default) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum("string3");
    value_column_->append_datum("string4");
    value_column_->append_datum(kNullDatum);

    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum("default");

    auto old_array = DatumArray{"string1", "string2"};
    auto new_array = DatumArray{"string1-new", "string2-new"};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
    ASSERT_EQ(5, result->size());
    EXPECT_EQ("string1-new", result->get(0).get_slice());
    EXPECT_EQ("string2-new", result->get(1).get_slice());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ("default", result->get(4).get_slice());
}

TEST_F(CelonisRemapValuesConstTest, string_empty_value_map_with_default) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum("string3");
    value_column_->append_datum("string4");

    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum("default-string-1");
    default_column_->append_datum("default-string-2");

    auto old_array = DatumArray{};
    auto new_array = DatumArray{};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
    ASSERT_EQ(4, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ("default-string-1", result->get(2).get_slice());
    EXPECT_EQ("default-string-2", result->get(3).get_slice());
}

TEST_F(CelonisRemapValuesConstTest, string_map_overwrite_with_default) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum("string3");
    value_column_->append_datum("string4");
    value_column_->append_datum(kNullDatum);

    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum("default");

    auto old_array = DatumArray{"string1", "string2", "string1"};
    auto new_array = DatumArray{"string1-old", "string2-new", "string1-new"};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
    ASSERT_EQ(5, result->size());
    EXPECT_EQ("string1-new", result->get(0).get_slice());
    EXPECT_EQ("string2-new", result->get(1).get_slice());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ("default", result->get(4).get_slice());
}

TEST_F(CelonisRemapValuesConstTest, string_without_default) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum("string3");
    value_column_->append_datum("string4");
    value_column_->append_datum(kNullDatum);

    auto old_array = DatumArray{"string1", "string2"};
    auto new_array = DatumArray{"string1-new", "string2-new"};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, false).value();
    ASSERT_EQ(5, result->size());
    EXPECT_EQ("string1-new", result->get(0).get_slice());
    EXPECT_EQ("string2-new", result->get(1).get_slice());
    EXPECT_EQ("string3", result->get(2).get_slice());
    EXPECT_EQ("string4", result->get(3).get_slice());
    EXPECT_TRUE(result->get(4).is_null());

}

TEST_F(CelonisRemapValuesConstTest, string_empty_value_map_without_default) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum("string3");
    value_column_->append_datum("string4");

    auto old_array = DatumArray{};
    auto new_array = DatumArray{};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, false).value();
    ASSERT_EQ(4, result->size());
    EXPECT_EQ("string1", result->get(0).get_slice());
    EXPECT_EQ("string2", result->get(1).get_slice());
    EXPECT_EQ("string3", result->get(2).get_slice());
    EXPECT_EQ("string4", result->get(3).get_slice());
}

TEST_F(CelonisRemapValuesConstTest, non_constant_string) {
    {
        const LogicalType LT = TYPE_VARCHAR;
        Prepare<LT>();

        AddRow(kNullDatum, DatumArray{"string1", kNullDatum}, DatumArray{"string2", "NULL"}, "default");
        AddRow("string", DatumArray{"string", "string", "string1"},
               DatumArray{"string-new-1", "string-new-2", kNullDatum}, "default");
        AddRow("NULL", DatumArray{"string1", kNullDatum}, DatumArray{"string2", "NULL"}, kNullDatum);
        AddRow("string", DatumArray{}, DatumArray{}, kNullDatum);

        const auto result = Run<LT>(true).value();
        ASSERT_EQ(4, result->size());
        EXPECT_EQ("NULL", result->get(0).get_slice());
        EXPECT_EQ("default", result->get(1).get_slice());
        EXPECT_TRUE(result->get(2).is_null());
        EXPECT_TRUE(result->get(3).is_null());
    }
    {
        const LogicalType LT = TYPE_VARCHAR;
        Prepare<LT>();

        AddRow("string", DatumArray{"string1", "string"}, DatumArray{"string2", "new-string"}, "default");
        AddRow("string3", DatumArray{"string1", "string"}, DatumArray{"string2", "new-string"}, "default");

        const auto result = Run<LT>(true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("new-string", result->get(0).get_slice());
        EXPECT_EQ("default", result->get(1).get_slice());
    }
    {
        const LogicalType LT = TYPE_VARCHAR;
        Prepare<LT>();

        AddRow("string", DatumArray{"string1", "string"}, DatumArray{"string2", "new-string"}, "default");
        AddRow("string3", DatumArray{"string1", "string"}, DatumArray{"string2", "new-string"}, "default");

        const auto result = Run<LT>(false).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("new-string", result->get(0).get_slice());
        EXPECT_EQ("string3", result->get(1).get_slice());
    }
}

TEST_F(CelonisRemapValuesConstTest, const_bigint) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();

    value_column_->append_datum(1L);
    value_column_->append_datum(2L);
    value_column_->append_datum(3L);
    value_column_->append_datum(4L);
    value_column_->append_datum(kNullDatum);
    value_column_->append_datum(kNullDatum);

    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(0L);
    default_column_->append_datum(kNullDatum);

    auto old_array = DatumArray{1L, 2L};
    auto new_array = DatumArray{100L, 200L};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
    ASSERT_EQ(6, result->size());
    EXPECT_EQ(100L, result->get(0).get_int64());
    EXPECT_EQ(200L, result->get(1).get_int64());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisRemapValuesConstTest, non_constant_bigint) {
    {
        const LogicalType LT = TYPE_BIGINT;
        Prepare<LT>();

        AddRow(2L, DatumArray{1L, 2L, 1L}, DatumArray{2L, 5L, 10L}, 0L);
        AddRow(kNullDatum, DatumArray{1L, 2L}, DatumArray{3L, 4L}, 0L);

        const auto result = Run<LT>(true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(5L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
    {
        const LogicalType LT = TYPE_BIGINT;
        Prepare<LT>();

        AddRow(1L, DatumArray{1L, 2L, 1L}, DatumArray{2L, 5L, 10L}, 0L);
        AddRow(kNullDatum, DatumArray{1L, 2L, kNullDatum}, DatumArray{3L, 4L, 5L}, 0L);

        const auto result = Run<LT>(true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(10L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
}

TEST_F(CelonisRemapValuesConstTest, const_value_column) {
    {
        const LogicalType LT = TYPE_DOUBLE;
        Prepare<LT>();

        value_column_->append_datum(1.0);
        value_column_ = ConstColumn::create(value_column_, 2);
        default_column_->append_datum(kNullDatum);
        default_column_->append_datum(kNullDatum);

        auto old_array = DatumArray{1.0, 2.0};
        auto new_array = DatumArray{100.0, 200.0};

        const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(100.0, result->get(0).get_double());
        EXPECT_EQ(100.0, result->get(1).get_double());
    }
    {
        const LogicalType LT = TYPE_DOUBLE;
        Prepare<LT>();

        value_column_->append_datum(kNullDatum);
        value_column_ = ConstColumn::create(value_column_, 2);
        default_column_->append_datum(kNullDatum);
        default_column_->append_datum(kNullDatum);

        auto old_array = DatumArray{1.0, 2.0};
        auto new_array = DatumArray{100.0, 200.0};

        const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        const LogicalType LT = TYPE_DOUBLE;
        Prepare<LT>();

        value_column_->append_datum(1.5);
        value_column_ = ConstColumn::create(value_column_, 2);
        default_column_->append_datum(1.0);
        default_column_->append_datum(kNullDatum);

        auto old_array = DatumArray{1.0, 2.0};
        auto new_array = DatumArray{100.0, 200.0};

        const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(1.0, result->get(0).get_double());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        const LogicalType LT = TYPE_DATETIME;
        Prepare<LT>();

        AddRow(TimestampValue::create(1970, 1, 2, 0, 0, 0), DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0)},
               DatumArray{TimestampValue::create(1970, 1, 20, 0, 0, 0)}, TimestampValue::create(1970, 1, 1, 0, 0, 0));

        value_column_ = ConstColumn::create(value_column_, 1);
        const auto result = Run<LT>(true).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(TimestampValue::create(1970, 1, 20, 0, 0, 0), result->get(0).get_timestamp());
    }
}

TEST_F(CelonisRemapValuesConstTest, const_double) {
    const LogicalType LT = TYPE_DOUBLE;
    Prepare<LT>();

    value_column_->append_datum(1.0);
    value_column_->append_datum(2.0);
    value_column_->append_datum(3.0);
    value_column_->append_datum(4.0);
    value_column_->append_datum(kNullDatum);

    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(0.0);

    auto old_array = DatumArray{1.0, 2.0};
    auto new_array = DatumArray{100.0, 200.0};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
    ASSERT_EQ(5, result->size());
    EXPECT_EQ(100.0, result->get(0).get_double());
    EXPECT_EQ(200.0, result->get(1).get_double());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(0.0, result->get(4).get_double());
}

TEST_F(CelonisRemapValuesConstTest, celonis_remap_values_non_constant_double) {
    {
        const LogicalType LT = TYPE_DOUBLE;
        Prepare<LT>();

        AddRow(2.0, DatumArray{1.0, 2.0, 1.0}, DatumArray{2.0, 5.0, 10.0}, 0.0);
        AddRow(kNullDatum, DatumArray{1.0, 2.0}, DatumArray{3.0, 4.0}, 0.0);

        const auto result = Run<LT>(true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(5.0, result->get(0).get_double());
        EXPECT_EQ(0.0, result->get(1).get_double());
    }
    {
        const LogicalType LT = TYPE_DOUBLE;
        Prepare<LT>();

        AddRow(1.0, DatumArray{1.0, 2.0, 1.0}, DatumArray{2.0, 5.0, 10.5}, 0.0);
        AddRow(kNullDatum, DatumArray{1.0, 2.0, kNullDatum}, DatumArray{3.0, 4.0, 5.5}, 0.0);

        const auto result = Run<LT>(true).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ(10.5, result->get(0).get_double());
        EXPECT_EQ(0.0, result->get(1).get_double());
    }
}

TEST_F(CelonisRemapValuesConstTest, const_datetime) {
    const LogicalType LT = TYPE_DATETIME;
    Prepare<LT>();

    value_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    value_column_->append_datum(TimestampValue::create(1970, 1, 2, 0, 0, 0));
    value_column_->append_datum(TimestampValue::create(1970, 1, 3, 0, 0, 0));
    value_column_->append_datum(TimestampValue::create(1970, 1, 4, 0, 0, 0));
    value_column_->append_datum(kNullDatum);

    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));

    auto old_array = DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0),
                                TimestampValue::create(1970, 1, 2, 0, 0, 0)};
    auto new_array = DatumArray{TimestampValue::create(1970, 1, 10, 0, 0, 0),
                                TimestampValue::create(1970, 1, 20, 0, 0, 0)};

    const auto result = RunConstantValueMap<LT>(old_array, new_array, true).value();
    ASSERT_EQ(5, result->size());
    EXPECT_EQ(TimestampValue::create(1970, 1, 10, 0, 0, 0), result->get(0).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1970, 1, 20, 0, 0, 0), result->get(1).get_timestamp());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
    EXPECT_EQ(TimestampValue::create(1970, 1, 1, 0, 0, 0), result->get(4).get_timestamp());
}

TEST_F(CelonisRemapValuesConstTest, celonis_remap_values_non_constant_datetime) {
    const LogicalType LT = TYPE_DATETIME;
    Prepare<LT>();

    AddRow(TimestampValue::create(1970, 1, 2, 0, 0, 0), DatumArray{TimestampValue::create(1970, 1, 2, 0, 0, 0)},
           DatumArray{TimestampValue::create(1970, 1, 20, 0, 0, 0)}, TimestampValue::create(1970, 1, 1, 0, 0, 0));
    AddRow(kNullDatum, DatumArray{}, DatumArray{}, TimestampValue::create(1970, 1, 1, 0, 0, 0));

    const auto result = Run<LT>(true).value();
    ASSERT_EQ(2, result->size());
    EXPECT_EQ(TimestampValue::create(1970, 1, 20, 0, 0, 0), result->get(0).get_timestamp());
    EXPECT_EQ(TimestampValue::create(1970, 1, 1, 0, 0, 0), result->get(1).get_timestamp());
}

} // namespace starrocks
