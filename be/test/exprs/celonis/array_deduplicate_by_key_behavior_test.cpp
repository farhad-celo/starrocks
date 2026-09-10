#include <glog/logging.h>
#include <gtest/gtest.h>

#include <vector>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_deduplicate_by_key.h"
#include "util.h"

namespace starrocks {

/**
 * Behavioral tests which target the VARCHAR/VARCHAR variant of the operator.
 */
class CelonisDeduplicateByKeyBehaviorTest : public testing::Test {
protected:
    static constexpr LogicalType TYPE = TYPE_VARCHAR;
};

TEST_F(CelonisDeduplicateByKeyBehaviorTest, TwoUniqueKeys) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    key_arrays->append_datum(DatumArray{"a", "b"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{"1", "2"});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{"1", "2"};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    ASSERT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, SingleDuplicateKey) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    key_arrays->append_datum(DatumArray{"a", "a"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{"1", "2"});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{"1"};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    ASSERT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, MultipleKeyGroupsInterleaved) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    key_arrays->append_datum(DatumArray{"a", "b", "c", "a", "b", "c"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{"1", "2", "3", "4", "5", "6"});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{"1", "2", "3"};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    EXPECT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, MultipleRows) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    key_arrays->append_datum(DatumArray{"a", "a"});
    key_arrays->append_datum(DatumArray{"b", "b"});
    key_arrays->append_datum(DatumArray{"c", "d"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{"1", "2"});
    value_arrays->append_datum(DatumArray{"3", "4"});
    value_arrays->append_datum(DatumArray{"5", "6"});

    constexpr auto expected_rows = 3;
    const auto expected_0 = DatumArray{"1"};
    const auto expected_1 = DatumArray{"3"};
    const auto expected_2 = DatumArray{"5", "6"};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    ASSERT_EQ(result->get(0).get_array(), expected_0);
    ASSERT_EQ(result->get(1).get_array(), expected_1);
    ASSERT_EQ(result->get(2).get_array(), expected_2);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, NullKeysAreSkipped) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    key_arrays->append_datum(DatumArray{"a", Datum{}});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{"1", "2"});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{"1"};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    EXPECT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, NullValuesAreNotSkipped) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    key_arrays->append_datum(DatumArray{"a", "b"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{"1", Datum{}});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{"1", Datum{}};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    EXPECT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, NullRowsAreSkipped) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    key_arrays->append_datum(Datum{});
    key_arrays->append_datum(DatumArray{"a"});
    key_arrays->append_datum(Datum{});
    key_arrays->append_datum(DatumArray{"b"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    value_arrays->append_datum(Datum{});
    value_arrays->append_datum(Datum{});
    value_arrays->append_datum(DatumArray{"1"});
    value_arrays->append_datum(DatumArray{"2"});

    constexpr auto expected_rows = 4;
    const auto expected = DatumArray{"2"};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);

    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_FALSE(result->get(3).is_null());

    EXPECT_EQ(result->get(3).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, KeyArrayOnlyNull) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    key_arrays->append_datum(DatumArray{Datum{}, Datum{}, Datum{}});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    value_arrays->append_datum(DatumArray{"1", "2", "3"});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    EXPECT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, ValueArrayOnlyNull) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    key_arrays->append_datum(DatumArray{"a", "b", "c"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    value_arrays->append_datum(DatumArray{Datum{}, Datum{}, Datum{}});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{Datum{}, Datum{}, Datum{}};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    EXPECT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, BothArraysOnlyNull) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    key_arrays->append_datum(DatumArray{Datum{}, Datum{}, Datum{}});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    value_arrays->append_datum(DatumArray{Datum{}, Datum{}, Datum{}});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    EXPECT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, KeyColumnConstNull) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    value_arrays->append_datum(DatumArray{"1"});
    value_arrays->append_datum(DatumArray{"2"});

    {
        auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
        key_arrays->append_datum(kNullDatum);

        // WHEN
        const auto result = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
                                    nullptr, {ConstColumn::create(key_arrays, 2), value_arrays})
                                    .value();

        // THEN
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto key_arrays = ColumnHelper::create_const_null_column(2);

        // WHEN
        const auto result =
                CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                        .value();

        // THEN
        EXPECT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, ValueColumnConstNull) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    key_arrays->append_datum(DatumArray{"a"});
    key_arrays->append_datum(DatumArray{"b"});

    {
        auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
        value_arrays->append_datum(kNullDatum);

        // WHEN
        const auto result = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
                                    nullptr, {key_arrays, ConstColumn::create(value_arrays, 2)})
                                    .value();

        // THEN
        ASSERT_EQ(2, result->size());

        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto value_arrays = ColumnHelper::create_const_null_column(2);

        // WHEN
        const auto result =
                CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                        .value();

        // THEN
        EXPECT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, BothColumnsConstNull) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    {
        auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
        key_arrays->append_datum(kNullDatum);

        auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
        value_arrays->append_datum(kNullDatum);

        // WHEN
        const auto result = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
                                    nullptr, {ConstColumn::create(key_arrays, 2), ConstColumn::create(value_arrays, 2)})
                                    .value();

        // THEN
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto key_arrays = ColumnHelper::create_const_null_column(2);
        auto value_arrays = ColumnHelper::create_const_null_column(2);

        // WHEN
        const auto result =
                CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                        .value();

        // THEN
        EXPECT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, BothColumnsConstNonNull) {
    // GIVEN
    auto key_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    key_arrays->append_datum(DatumArray{"a", "a", "b"});
    auto value_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    value_arrays->append_datum(DatumArray{"1", "2", "3"});

    const DatumArray expected{"1", "3"};

    // WHEN
    const auto result_or = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
            nullptr, {ConstColumn::create(key_arrays, 2), ConstColumn::create(value_arrays, 2)});
    ASSERT_TRUE(result_or.ok()) << result_or.status().to_string();
    auto result = result_or.value();

    // THEN
    ASSERT_TRUE(result->is_constant());

    // The expression layer resizes constant results to the input chunk size.
    result->resize(2);

    EXPECT_EQ(result->get(0).get_array(), expected);
    EXPECT_EQ(result->get(1).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, BothColumnsConstWithOneNullValue) {
    // GIVEN
    auto key_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    key_arrays->append_datum(DatumArray{"a", "a", "b"});
    auto value_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), true);
    value_arrays->append_datum(DatumArray{Datum{}, "10", "20"});

    // The first occurrence wins even if it's value is nu
    const DatumArray expected{Datum{}, "20"};

    // WHEN
    const auto result_or = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
            nullptr, {ConstColumn::create(key_arrays, 2), ConstColumn::create(value_arrays, 2)});
    ASSERT_TRUE(result_or.ok()) << result_or.status().to_string();
    auto result = result_or.value();

    // THEN
    ASSERT_TRUE(result->is_constant());

    result->resize(2);

    EXPECT_EQ(result->get(0).get_array(), expected);
    EXPECT_EQ(result->get(1).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, BothColumnsConstAllKeysNull) {
    // GIVEN
    auto key_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), true);
    key_arrays->append_datum(DatumArray{Datum{}, Datum{}});
    auto value_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    value_arrays->append_datum(DatumArray{"1", "2"});

    const DatumArray expected{};

    // WHEN
    const auto result_or = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
            nullptr, {ConstColumn::create(key_arrays, 2), ConstColumn::create(value_arrays, 2)});
    ASSERT_TRUE(result_or.ok()) << result_or.status().to_string();
    auto result = result_or.value();

    // THEN
    ASSERT_TRUE(result->is_constant());

    result->resize(2);

    EXPECT_EQ(result->get(0).get_array(), expected);
    EXPECT_EQ(result->get(1).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, ConstKeysVaryingValues) {
    // GIVEN
    auto key_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    key_arrays->append_datum(DatumArray{"a", "a", "b"});
    auto value_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    value_arrays->append_datum(DatumArray{"1", "2", "3"});
    value_arrays->append_datum(DatumArray{"4", "5", "6"});

    const DatumArray expected_0{"1", "3"};
    const DatumArray expected_1{"4", "6"};

    // WHEN
    const auto result_or = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
            nullptr, {ConstColumn::create(key_arrays, 2), value_arrays});
    ASSERT_TRUE(result_or.ok()) << result_or.status().to_string();
    const auto& result = result_or.value();

    // THEN
    ASSERT_FALSE(result->is_constant());

    EXPECT_EQ(result->get(0).get_array(), expected_0);
    EXPECT_EQ(result->get(1).get_array(), expected_1);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, VaryingKeysConstValues) {
    // GIVEN
    auto key_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    key_arrays->append_datum(DatumArray{"a", "a", "b"});
    key_arrays->append_datum(DatumArray{"b", "c", "c"});
    auto value_arrays = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    value_arrays->append_datum(DatumArray{"10", "20", "30"});

    const DatumArray expected_0{"10", "30"};
    const DatumArray expected_1{"10", "20"};

    // WHEN
    const auto result_or = CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(
            nullptr, {key_arrays, ConstColumn::create(value_arrays, 2)});
    ASSERT_TRUE(result_or.ok()) << result_or.status().to_string();
    const auto& result = result_or.value();

    // THEN
    ASSERT_FALSE(result->is_constant());

    EXPECT_EQ(result->get(0).get_array(), expected_0);
    EXPECT_EQ(result->get(1).get_array(), expected_1);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, ErrorWhenArraysAreNotSameSize) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    FunctionContext context{};

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    key_arrays->append_datum(DatumArray{"a", "b", "c"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    value_arrays->append_datum(DatumArray{"1", "2"});

    const auto expected_status = Status::InvalidArgument(
            "Invalid deduplicate by key: expected arrays of equal size but found key array of size [3] and value array "
            "of size [2].");

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(&context, {key_arrays, value_arrays});

    // THEN
    ASSERT_EQ(result.status().code_as_string(), expected_status.code_as_string());
    ASSERT_EQ(result.status().message(), expected_status.message());
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, EmptyInputColumn) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);

    constexpr auto expected_rows = 0;

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, NonEmptyInputColumnWithEmptyArray) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    key_arrays->append_datum(DatumArray{});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    EXPECT_EQ(result->get(0).get_array(), expected);
}

TEST_F(CelonisDeduplicateByKeyBehaviorTest, ValueColumnIsNotNullableButOutputContainsNull) {
    // GIVEN
    TypeDescriptor ARRAY_TYPE = celonis::array_type(TYPE);

    auto key_arrays = ColumnHelper::create_column(ARRAY_TYPE, true);
    key_arrays->append_datum(Datum{});
    key_arrays->append_datum(DatumArray{"a"});

    auto value_arrays = ColumnHelper::create_column(ARRAY_TYPE, false);
    value_arrays->append_datum(DatumArray{"1"});
    value_arrays->append_datum(DatumArray{"2"});

    constexpr auto expected_rows = 2;
    const auto expected = DatumArray{"2"};

    // WHEN
    const auto result =
            CelonisArrayDeduplicateByKey<TYPE, TYPE>::array_deduplicate_by_key(nullptr, {key_arrays, value_arrays})
                    .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);

    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_FALSE(result->get(1).is_null());

    EXPECT_EQ(result->get(1).get_array(), expected);
}

} // namespace starrocks
