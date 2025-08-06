#include "exprs/celonis/multi_in.h"

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "testutil/function_utils.h"
#include "util.h"
#include "util/defer_op.h"

#include <gtest/gtest.h>

namespace starrocks {

class CelonisMultiInTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);

    StatusOr<ColumnPtr> RunConstMatchLists(const Columns& input_fields, const Columns& match_fields) {
        auto utils = std::make_shared<FunctionUtils>();
        auto input_struct_col = StructColumn::create(input_fields);
        auto match_struct_col = StructColumn::create(match_fields);
        auto const_match_struct_col = ConstColumn::create(match_struct_col, input_struct_col->size());
        Columns constant_columns;
        constant_columns.push_back(nullptr);
        constant_columns.push_back(const_match_struct_col);
        utils->get_fn_ctx()->set_constant_columns(std::move(constant_columns));
        RETURN_IF_ERROR(CelonisMultiIn::prepare(utils->get_fn_ctx(), FunctionContext::FRAGMENT_LOCAL));
        Columns columns;
        columns.push_back(input_struct_col);
        columns.push_back(match_struct_col);
        StatusOr<ColumnPtr> result = CelonisMultiIn::multi_in(utils->get_fn_ctx(), columns);
        RETURN_IF_ERROR(CelonisMultiIn::close(utils->get_fn_ctx(), FunctionContext::FRAGMENT_LOCAL));
        return result;
    }

    StatusOr<ColumnPtr> Run(const Columns& input_fields, const Columns& match_fields) {
        auto utils = std::make_shared<FunctionUtils>();
        auto input_struct_col = StructColumn::create(input_fields);
        auto match_struct_col = StructColumn::create(match_fields);
        Columns constant_columns;
        constant_columns.push_back(nullptr);
        constant_columns.push_back(nullptr);
        utils->get_fn_ctx()->set_constant_columns(std::move(constant_columns));
        DeferOp close_fragment_local([&utils] {
            CelonisMultiIn::close(utils->get_fn_ctx(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisMultiIn::prepare(utils->get_fn_ctx(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([&utils] {
            CelonisMultiIn::close(utils->get_fn_ctx(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisMultiIn::prepare(utils->get_fn_ctx(), FunctionContext::THREAD_LOCAL));
        Columns columns;
        columns.push_back(input_struct_col);
        columns.push_back(match_struct_col);
        StatusOr<ColumnPtr> result = CelonisMultiIn::multi_in(utils->get_fn_ctx(), columns);
        return result;
    }

};

TEST_F(CelonisMultiInTest, empty_input) {
    auto ints = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    auto int_arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    auto result = RunConstMatchLists({ints}, {int_arrays}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisMultiInTest, non_const_match_lists) {
    auto ints_1 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    ints_1->append_datum(1);
    ints_1->append_datum(3);
    ints_1->append_datum(5);
    auto ints_2 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    ints_2->append_datum(2);
    ints_2->append_datum(4);
    ints_2->append_datum(6);
    auto int_arrays_1 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    auto int_arrays_2 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    int_arrays_1->append_datum(DatumArray{5, 1});
    int_arrays_2->append_datum(DatumArray{6, 2});

    int_arrays_1->append_datum(DatumArray{7, 1});
    int_arrays_2->append_datum(DatumArray{8, 2});

    int_arrays_1->append_datum(DatumArray{1});
    int_arrays_2->append_datum(DatumArray{2});
    auto result = Run({ints_1, ints_2}, {int_arrays_1, int_arrays_2});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "CELONIS_MULTI_IN: the second argument (struct_of_arrays) must be constant.");
}

TEST_F(CelonisMultiInTest, match_fields_mismatch) {
    {
        auto ints_1 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
        ints_1->append_datum(1);
        ints_1->append_datum(3);
        ints_1->append_datum(5);
        auto ints_2 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
        ints_2->append_datum(2);
        ints_2->append_datum(4);
        ints_2->append_datum(6);
        auto int_arrays_1 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        auto int_arrays_2 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        int_arrays_1->append_datum(DatumArray{5, 1});
        int_arrays_2->append_datum(DatumArray{6, 2, 4});
        auto result = RunConstMatchLists({ints_1, ints_2}, {int_arrays_1, int_arrays_2}).value();
        EXPECT_EQ(3, result->size());
        EXPECT_EQ(false, result->get(0).get_uint8());
        EXPECT_EQ(false, result->get(1).get_uint8());
        EXPECT_EQ(false, result->get(2).get_uint8());
    }
    {
        auto ints_1 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
        ints_1->append_datum(1);
        ints_1->append_datum(3);
        ints_1->append_datum(5);
        auto int_arrays_1 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        auto int_arrays_2 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        int_arrays_1->append_datum(DatumArray{5, 1});
        int_arrays_2->append_datum(DatumArray{6, 2});
        auto result = RunConstMatchLists({ints_1}, {int_arrays_1, int_arrays_2}).value();
        EXPECT_EQ(3, result->size());
        EXPECT_EQ(false, result->get(0).get_uint8());
        EXPECT_EQ(false, result->get(1).get_uint8());
        EXPECT_EQ(false, result->get(2).get_uint8());
    }
}

TEST_F(CelonisMultiInTest, single_field) {
    auto ints = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    ints->append_datum(1);
    ints->append_datum(3);
    ints->append_datum(5);
    auto int_arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    int_arrays->append_datum(DatumArray{5, 4});
    auto result = RunConstMatchLists({ints}, {int_arrays}).value();
    EXPECT_EQ(3, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
}

TEST_F(CelonisMultiInTest, multiple_fields) {
    auto ints_1 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    ints_1->append_datum(1);
    ints_1->append_datum(3);
    ints_1->append_datum(5);
    auto ints_2 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    ints_2->append_datum(2);
    ints_2->append_datum(4);
    ints_2->append_datum(6);
    auto int_arrays_1 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    auto int_arrays_2 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    int_arrays_1->append_datum(DatumArray{5, 1});
    int_arrays_2->append_datum(DatumArray{6, 2});
    auto result = RunConstMatchLists({ints_1, ints_2}, {int_arrays_1, int_arrays_2}).value();
    EXPECT_EQ(3, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
}

TEST_F(CelonisMultiInTest, multiple_fields_with_different_types) {
    auto timestamps = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    timestamps->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
    auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    strings->append_datum("AA");
    strings->append_datum("BB");
    strings->append_datum("CC");
    auto timestamp_arrays = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, false);
    auto string_arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    timestamp_arrays->append_datum(
            DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 2, 0, 0, 0),
                       TimestampValue::create(1970, 1, 3, 0, 0, 0)});
    string_arrays->append_datum(DatumArray{"AA", "AA", "AA"});
    auto result = RunConstMatchLists({timestamps, strings}, {timestamp_arrays, string_arrays}).value();
    EXPECT_EQ(3, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
}

TEST_F(CelonisMultiInTest, floating_number_comparison) {
    auto ints_1 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    ints_1->append_datum(1);
    ints_1->append_datum(3);
    ints_1->append_datum(5);
    ints_1->append_datum(7);
    auto ints_2 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
    ints_2->append_datum(2);
    ints_2->append_datum(4);
    ints_2->append_datum(6);
    ints_2->append_datum(8);
    auto double_arrays_1 = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, false);
    auto double_arrays_2 = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, false);
    double_arrays_1->append_datum(DatumArray{5.0, 1.0, 3.001, 7.0});
    double_arrays_2->append_datum(DatumArray{6.0, 2.0, 4.0, 8.0});
    auto result = RunConstMatchLists({ints_1, ints_2}, {double_arrays_1, double_arrays_2}).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisMultiInTest, null_match) {
    {
        auto ints_1 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), false);
        ints_1->append_datum(1);
        ints_1->append_datum(3);
        ints_1->append_datum(5);
        ints_1->append_datum(7);
        auto ints_2 = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
        ints_2->append_datum(kNullDatum);
        ints_2->append_datum(kNullDatum);
        ints_2->append_datum(kNullDatum);
        ints_2->append_datum(kNullDatum);
        auto int_arrays_1 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        auto int_arrays_2 = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        int_arrays_1->append_datum(DatumArray{1, 3, 7});
        int_arrays_2->append_datum(DatumArray{2, kNullDatum, 8});
        auto result = RunConstMatchLists({ints_1, ints_2}, {int_arrays_1, int_arrays_2}).value();
        EXPECT_EQ(4, result->size());
        EXPECT_EQ(false, result->get(0).get_uint8());
        EXPECT_EQ(true, result->get(1).get_uint8());
        EXPECT_EQ(false, result->get(2).get_uint8());
        EXPECT_EQ(false, result->get(3).get_uint8());
    }
    {
        auto ints = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
        ints->append_datum(1);
        ints->append_datum(2);
        ints->append_datum(3);
        ints->append_datum(kNullDatum);
        ints->append_datum(kNullDatum);
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("A");
        strings->append_datum("");
        strings->append_datum(kNullDatum);
        strings->append_datum("D");
        strings->append_datum(kNullDatum);
        auto int_arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        auto string_arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        int_arrays->append_datum(DatumArray{1, 3, 4, kNullDatum});
        string_arrays->append_datum(DatumArray{"A", kNullDatum, "D", kNullDatum});
        auto result = RunConstMatchLists({ints, strings}, {int_arrays, string_arrays}).value();
        EXPECT_EQ(5, result->size());
        EXPECT_EQ(true, result->get(0).get_uint8());
        EXPECT_EQ(false, result->get(1).get_uint8());
        EXPECT_EQ(true, result->get(2).get_uint8());
        EXPECT_EQ(false, result->get(3).get_uint8());
        EXPECT_EQ(true, result->get(4).get_uint8());
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum(kNullDatum);
        strings->append_datum("");
        strings->append_datum("3");
        strings->append_datum("4");
        auto ints = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
        ints->append_datum(1);
        ints->append_datum(2);
        ints->append_datum(3);
        ints->append_datum(4);
        auto string_arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto int_arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        string_arrays->append_datum(DatumArray{kNullDatum, "3"});
        int_arrays->append_datum(DatumArray{1, 3});
        auto result = RunConstMatchLists({strings, ints}, {string_arrays, int_arrays}).value();
        EXPECT_EQ(4, result->size());
        EXPECT_EQ(true, result->get(0).get_uint8());
        EXPECT_EQ(false, result->get(1).get_uint8());
        EXPECT_EQ(true, result->get(2).get_uint8());
        EXPECT_EQ(false, result->get(3).get_uint8());
    }
}

} // namespace starrocks
