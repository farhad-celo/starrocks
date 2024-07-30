#include "exprs/celonis/array_functions.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "runtime/types.h"
#include "util.h"

#include <gtest/gtest.h>

namespace starrocks {

class CelonisArrayFunctionsTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_BOOLEAN = celonis::array_type(TYPE_BOOLEAN);
    TypeDescriptor TYPE_ARRAY_TINYINT = celonis::array_type(TYPE_TINYINT);
    TypeDescriptor TYPE_ARRAY_SMALLINT = celonis::array_type(TYPE_SMALLINT);
    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_LARGEINT = celonis::array_type(TYPE_LARGEINT);
    TypeDescriptor TYPE_ARRAY_FLOAT = celonis::array_type(TYPE_FLOAT);
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_DATE = celonis::array_type(TYPE_DATE);
    TypeDescriptor TYPE_ARRAY_DATETIME = celonis::array_type(TYPE_DATETIME);
};

// NOLINTNEXTLINE
TEST_F(CelonisArrayFunctionsTest, array_is_sorted_empty_array) {
    // array_is_sorted([])
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        array->append_datum(Datum(DatumArray{}));

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(1, result->get(0).get_int8());
    }
    // multiple lines:
    //  array_is_sorted([]);
    //  array_is_sorted([]);
    //  array_is_sorted([]);
    //  array_is_sorted([]);
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        for (auto i = 0; i < 4; ++i)
            array->append_datum(Datum(DatumArray{}));

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (auto i = 0; i < array->size(); ++i)
            EXPECT_EQ(1, result->get(i).get_int8());
    }
}

// NOLINTNEXTLINE
TEST_F(CelonisArrayFunctionsTest, array_is_sorted_no_null) {
    /// Test class:
    ///  - the array elements has NO NULL.

    // array_is_sorted(array<boolean>[]) : 1
    // array_is_sorted(array<boolean>[0]) : 1
    // array_is_sorted(array<boolean>[1]) : 1
    // array_is_sorted(array<boolean>[0,0]) : 1
    // array_is_sorted(array<boolean>[0,1]) : 1
    // array_is_sorted(array<boolean>[1,1]) : 1
    // array_is_sorted(array<boolean>[0,0,0]) : 1
    // array_is_sorted(array<boolean>[0,0,1]) : 1
    // array_is_sorted(array<boolean>[0,1,1]) : 1
    // array_is_sorted(array<boolean>[1,1,1]) : 1
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_BOOLEAN, false);
        array->append_datum(Datum(DatumArray{}));
        array->append_datum(DatumArray{(int8_t) false});
        array->append_datum(DatumArray{(int8_t) true});
        array->append_datum(DatumArray{(int8_t) false, (int8_t) false});
        array->append_datum(DatumArray{(int8_t) false, (int8_t) true});
        array->append_datum(DatumArray{(int8_t) true, (int8_t) true});
        array->append_datum(DatumArray{(int8_t) false, (int8_t) false, (int8_t) false});
        array->append_datum(DatumArray{(int8_t) false, (int8_t) false, (int8_t) true});
        array->append_datum(DatumArray{(int8_t) false, (int8_t) true, (int8_t) true});
        array->append_datum(DatumArray{(int8_t) true, (int8_t) true, (int8_t) true});

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (auto i = 0; i < array->size(); ++i)
            EXPECT_EQ(1, result->get(i).get_int8());
    }

    // array_is_sorted(array<boolean>[1,0]) : 0
    // array_is_sorted(array<boolean>[0,1,0]) : 0
    // array_is_sorted(array<boolean>[1,0,0]) : 0
    // array_is_sorted(array<boolean>[1,0,1]) : 0
    // array_is_sorted(array<boolean>[1,1,0]) : 0
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_BOOLEAN, false);
        array->append_datum(DatumArray{(int8_t) true, (int8_t) false});
        array->append_datum(DatumArray{(int8_t) false, (int8_t) true, (int8_t) false});
        array->append_datum(DatumArray{(int8_t) true, (int8_t) false, (int8_t) false});
        array->append_datum(DatumArray{(int8_t) true, (int8_t) false, (int8_t) true});
        array->append_datum(DatumArray{(int8_t) true, (int8_t) true, (int8_t) false});

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (auto i = 0; i < array->size(); ++i)
            EXPECT_EQ(0, result->get(i).get_int8());
    }
}

// NOLINTNEXTLINE
TEST_F(CelonisArrayFunctionsTest, array_is_sorted_has_null_element) {
    // array_is_sorted([NULL])
    // array_is_sorted([NULL, NULL, NULL])
    // array_is_sorted(["abc"])
    // array_is_sorted([NULL, NULL, "abc"])
    // array_is_sorted(["abc", "def"])
    // array_is_sorted([NULL, NULL, NULL, "abc", "def"])
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        array->append_datum(DatumArray{Datum{}});
        array->append_datum(DatumArray{Datum{}, Datum{}, Datum{}});
        array->append_datum(DatumArray{"abc"});
        array->append_datum(DatumArray{Datum{}, Datum{}, "abc"});
        array->append_datum(DatumArray{"abc", "def"});
        array->append_datum(DatumArray{Datum{}, Datum{}, "abc", "def"});

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (int i = 0; i < result->size(); ++i)
            EXPECT_EQ(1, result->get(i).get_int8());
    }

    // array_is_sorted(["abd", "abc"])
    // array_is_sorted([NULL, NULL, "abc", NULL])
    // array_is_sorted([NULL, NULL, "abc", NULL, "def"])
    // array_is_sorted([NULL, NULL, "abc", "def", NULL])
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        array->append_datum(DatumArray{"abd", "abc"});
        array->append_datum(DatumArray{Datum{}, Datum{}, "abc", Datum{}});
        array->append_datum(DatumArray{Datum{}, Datum{}, "abc", Datum{}, "def"});
        array->append_datum(DatumArray{Datum{}, Datum{}, "abc", "def", Datum{}});

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (int i = 0; i < result->size(); ++i)
            EXPECT_EQ(0, result->get(i).get_int8());
    }

    // array_is_sorted(ARRAY<TINYINT>[1, 2, 3])
    // array_is_sorted(ARRAY<TINYINT>[NULL, NULL, 1, 2, 3])
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_TINYINT, true);
        array->append_datum(DatumArray{(int8_t) 1, (int8_t) 2, (int8_t) 3});
        array->append_datum(DatumArray{Datum{}, Datum{}, (int8_t) 1, (int8_t) 2, (int8_t) 3});

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (int i = 0; i < result->size(); ++i)
            EXPECT_EQ(1, result->get(i).get_int8());
    }

    // array_is_sorted(ARRAY<TINYINT>[NULL, NULL, 1, NULL, 2, 3])
    // array_is_sorted(ARRAY<TINYINT>[3, 1, 2])
    // array_is_sorted(ARRAY<TINYINT>[1, 2, 3, NULL])
    {
        auto array = ColumnHelper::create_column(TYPE_ARRAY_TINYINT, true);
        array->append_datum(DatumArray{Datum{}, Datum{}, (int8_t) 1, Datum{}, (int8_t) 2, (int8_t) 3});
        array->append_datum(DatumArray{(int8_t) 3, (int8_t) 1, (int8_t) 2});
        array->append_datum(DatumArray{(int8_t) 1, (int8_t) 2, (int8_t) 3, Datum{}});

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (int i = 0; i < result->size(); ++i)
            EXPECT_EQ(0, result->get(i).get_int8());
    }
}

// NOLINTNEXTLINE
TEST_F(CelonisArrayFunctionsTest, array_is_sorted_nullable_array) {
    // array_is_sorted(NULL) : NULL
    // array_is_sorted(NULL) : NULL
    {
        auto array = ColumnHelper::create_const_null_column(2);

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        for (int i = 0; i < result->size(); ++i)
            EXPECT_TRUE(result->get(i).is_null());
    }

    // array_is_sorted(NULL) : NULL
    // array_is_sorted(ARRAY<TINYINT>[2, 3, 5]) : 1
    {
        // The data column must have the same size as null column, therefore there are two tuples here.
        // The value in the data column does not matter if the tuple is NULL.
        auto data = ColumnHelper::create_column(TYPE_ARRAY_TINYINT, false);
        data->append_datum(DatumArray{Datum()});
        data->append_datum(DatumArray{(int8_t) 2, (int8_t) 3, (int8_t) 5});

        auto null_col = NullColumn::create();
        null_col->append_datum(Datum((uint8_t) 1));
        null_col->append_datum(Datum((uint8_t) 0));

        auto array = NullableColumn::create(std::move(data), null_col);

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_EQ(1, result->get(1).get_int8());
    }

    // array_is_sorted(ARRAY<TINYINT>[5, 2, 3]) : 0
    // array_is_sorted(NULL) : NULL
    {
        // The data column must have the same size as null column, therefore there are two tuples here.
        // The value in the data column does not matter if the tuple is NULL.
        auto data = ColumnHelper::create_column(TYPE_ARRAY_TINYINT, false);
        data->append_datum(DatumArray{(int8_t) 5, (int8_t) 2, (int8_t) 3});
        data->append_datum(DatumArray{Datum()});

        auto null_col = NullColumn::create();
        null_col->append_datum(Datum((uint8_t) 0));
        null_col->append_datum(Datum((uint8_t) 1));

        auto array = NullableColumn::create(std::move(data), null_col);

        const auto result = CelonisArrayFunctions::array_is_sorted(nullptr, {array}).value();
        ASSERT_EQ(array->size(), result->size());
        EXPECT_EQ(0, result->get(0).get_int8());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisArrayFunctionsTest, dedup_sorted_by_int) {
    {
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, 3, 4});
        input_array->append_datum(DatumArray{10, 11, 20, 21, 22, 30, 31});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        key_array->append_datum(DatumArray{"e1", "e2", "e3", "e4"});
        key_array->append_datum(DatumArray{"e1", "e1", "e2", "e2", "e2", "e3", "e3"});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_EQ(1, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(2, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(3, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(4, result->get(0).get_array()[3].get_int32());
        ASSERT_EQ(3, result->get(1).get_array().size());
        EXPECT_EQ(10, result->get(1).get_array()[0].get_int32());
        EXPECT_EQ(20, result->get(1).get_array()[1].get_int32());
        EXPECT_EQ(30, result->get(1).get_array()[2].get_int32());
    }
    {
        // input_array has NULL elements.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{10, 11, Datum{}, 21, 22, 30, Datum{}});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        key_array->append_datum(DatumArray{"e1", "e1", "e2", "e2", "e2", "e3", "e3"});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(3, result->get(0).get_array().size());
        EXPECT_EQ(10, result->get(0).get_array()[0].get_int32());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_EQ(30, result->get(0).get_array()[2].get_int32());
    }
}

TEST_F(CelonisArrayFunctionsTest, dedup_sorted_by_varchar) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "21", "three0", "31", "32"});

    auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    key_array->append_datum(DatumArray{"one", "two", "two", "three", "three", "three"});

    const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ("10", result->get(0).get_array()[0].get_slice());
    EXPECT_EQ("20", result->get(0).get_array()[1].get_slice());
    EXPECT_EQ("three0", result->get(0).get_array()[2].get_slice());
}

TEST_F(CelonisArrayFunctionsTest, dedup_sorted_by_datetime) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, false);
    input_array->append_datum(DatumArray{TimestampValue::create(2017, 10, 1, 1, 32, 32),
                                         TimestampValue::create(2017, 10, 2, 1, 32, 32),
                                         TimestampValue::create(2017, 10, 3, 1, 32, 32),
                                         TimestampValue::create(2017, 10, 3, 2, 32, 32),
                                         TimestampValue::create(2017, 10, 3, 3, 32, 32)});

    auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    key_array->append_datum(DatumArray{"e1", "e2", "e3", "e3", "e3"});

    const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(TimestampValue::create(2017, 10, 1, 1, 32, 32), result->get(0).get_array()[0].get_timestamp());
    EXPECT_EQ(TimestampValue::create(2017, 10, 2, 1, 32, 32), result->get(0).get_array()[1].get_timestamp());
    EXPECT_EQ(TimestampValue::create(2017, 10, 3, 1, 32, 32), result->get(0).get_array()[2].get_timestamp());
}

TEST_F(CelonisArrayFunctionsTest, dedup_sorted_by_null_array) {
    {
        // input_array has NULL and the corresponding key_array is not NULL and non-empty.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
        input_array->append_datum(Datum{});
        input_array->append_datum(DatumArray{1, 2, 3, 4});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        key_array->append_datum(DatumArray{"e1", "e2"});
        key_array->append_datum(DatumArray{"e1", "e2", "e2", "e4"});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_TRUE(result->is_null(0));
        ASSERT_EQ(3, result->get(1).get_array().size());
        EXPECT_EQ(1, result->get(1).get_array()[0].get_int32());
        EXPECT_EQ(2, result->get(1).get_array()[1].get_int32());
        EXPECT_EQ(4, result->get(1).get_array()[2].get_int32());
    }
    {
        // input_array has NULL and the corresponding key_array is not NULL and empty.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
        input_array->append_datum(Datum{});
        input_array->append_datum(DatumArray{1, 2, 3, 4});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        key_array->append_datum(DatumArray{});
        key_array->append_datum(DatumArray{"e1", "e2", "e2", "e4"});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_TRUE(result->is_null(0));
        ASSERT_EQ(3, result->get(1).get_array().size());
        EXPECT_EQ(1, result->get(1).get_array()[0].get_int32());
        EXPECT_EQ(2, result->get(1).get_array()[1].get_int32());
        EXPECT_EQ(4, result->get(1).get_array()[2].get_int32());
    }
    {
        // key_array has NULL and corresponding input_array is NULL.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
        input_array->append_datum(DatumArray{1, 2, 3, 4});
        input_array->append_datum(Datum{});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        key_array->append_datum(DatumArray{"e1", "e2", "e3", "e4"});
        key_array->append_datum(Datum{});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_EQ(1, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(2, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(3, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(4, result->get(0).get_array()[3].get_int32());
        ASSERT_TRUE(result->is_null(1));
    }
    {
        // key_array has NULL and corresponding input_array is empty.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, 3, 4});
        input_array->append_datum(DatumArray{});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        key_array->append_datum(DatumArray{"e1", "e2", "e3", "e4"});
        key_array->append_datum(Datum{});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_EQ(1, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(2, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(3, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(4, result->get(0).get_array()[3].get_int32());
        ASSERT_EQ(0, result->get(1).get_array().size());
    }
    {
        // key_array has NULL and corresponding input_array is non-empty.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, 3, 4});
        input_array->append_datum(DatumArray{1, 2, 3, 4});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        key_array->append_datum(DatumArray{"e1", "e2", "e3", "e4"});
        key_array->append_datum(Datum{});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "The size of input_array and key_array should not be different.");
    }
}

TEST_F(CelonisArrayFunctionsTest, dedup_sorted_by_empty_input_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{1, 2, 3, 4});
    input_array->append_datum(DatumArray{});

    auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    key_array->append_datum(DatumArray{"e1", "e2", "e3", "e4"});
    key_array->append_datum(DatumArray{});

    const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(4, result->get(0).get_array().size());
    EXPECT_EQ(1, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(2, result->get(0).get_array()[1].get_int32());
    EXPECT_EQ(3, result->get(0).get_array()[2].get_int32());
    EXPECT_EQ(4, result->get(0).get_array()[3].get_int32());
    ASSERT_EQ(0, result->get(1).get_array().size());
}

TEST_F(CelonisArrayFunctionsTest, dedup_sorted_by_null_elements) {
    // key_array has NULL elements.
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{1, 2, 3, 4});

    auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    key_array->append_datum(DatumArray{Datum{}, "e1", "e2", "e2"});

    const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "key_array should not have null elements.");
}

TEST_F(CelonisArrayFunctionsTest, dedup_sorted_by_array_size_mismatch) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{1, 2, 3, 4});

    auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    key_array->append_datum(DatumArray{"e1", "e2", "e2"});

    const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(),
              "The size of input_array and key_array should not be different.");
}

TEST_F(CelonisArrayFunctionsTest, array_lag_datetime) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, false);
    input_array->append_datum(DatumArray{TimestampValue::create(2020, 8, 10, 1, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 2, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 3, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 4, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 5, 32, 32)});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(3L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(5, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());
    EXPECT_EQ(TimestampValue::create(2020, 8, 10, 1, 32, 32), result->get(0).get_array()[3].get_timestamp());
    EXPECT_EQ(TimestampValue::create(2020, 8, 10, 2, 32, 32), result->get(0).get_array()[4].get_timestamp());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_int) {
    {
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, 3, 4, 5, 6});

        auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        offset_array->append_datum(3L);

        const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(6, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_EQ(1, result->get(0).get_array()[3].get_int32());
        EXPECT_EQ(2, result->get(0).get_array()[4].get_int32());
        EXPECT_EQ(3, result->get(0).get_array()[5].get_int32());
    }

    {
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, kNullDatum, 4, 5, 6});

        auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        offset_array->append_datum(2L);

        const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(6, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(1, result->get(0).get_array()[3].get_int32());
        EXPECT_EQ(2, result->get(0).get_array()[4].get_int32());
        EXPECT_EQ(4, result->get(0).get_array()[5].get_int32());
    }
}

TEST_F(CelonisArrayFunctionsTest, array_lag_large_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{1, 2, 3, 4, 5, 6});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(6L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(6, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());
    EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    EXPECT_TRUE(result->get(0).get_array()[4].is_null());
    EXPECT_TRUE(result->get(0).get_array()[5].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_varchar) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", kNullDatum, "three0", "31", "32"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(6, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_EQ("10", result->get(0).get_array()[1].get_slice());
    EXPECT_EQ("20", result->get(0).get_array()[2].get_slice());
    EXPECT_EQ("20", result->get(0).get_array()[3].get_slice());
    EXPECT_EQ("three0", result->get(0).get_array()[4].get_slice());
    EXPECT_EQ("31", result->get(0).get_array()[5].get_slice());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_all_null) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{kNullDatum, kNullDatum, kNullDatum, kNullDatum});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(4, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());
    EXPECT_TRUE(result->get(0).get_array()[3].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_empty_column) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_empty_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{});
    input_array->append_datum(DatumArray{"1", "2", "3"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);
    offset_array->append_datum(2L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(0, result->get(0).get_array().size());
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_TRUE(result->get(1).get_array()[1].is_null());
    EXPECT_EQ("1", result->get(1).get_array()[2].get_slice());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_multiple_nulls) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", kNullDatum, "30", kNullDatum, kNullDatum, "40", "50", "60"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(2L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(9, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_EQ("10", result->get(0).get_array()[2].get_slice());
    EXPECT_EQ("10", result->get(0).get_array()[3].get_slice());
    EXPECT_EQ("20", result->get(0).get_array()[4].get_slice());
    EXPECT_EQ("20", result->get(0).get_array()[5].get_slice());
    EXPECT_EQ("20", result->get(0).get_array()[6].get_slice());
    EXPECT_EQ("30", result->get(0).get_array()[7].get_slice());
    EXPECT_EQ("40", result->get(0).get_array()[8].get_slice());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_multiple_arrays) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});
    input_array->append_datum(DatumArray{"100", "200", "300"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);
    offset_array->append_datum(2L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(3, result->get(0).get_array().size());
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_EQ("10", result->get(0).get_array()[1].get_slice());
    EXPECT_EQ("20", result->get(0).get_array()[2].get_slice());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_TRUE(result->get(1).get_array()[1].is_null());
    EXPECT_EQ("100", result->get(1).get_array()[2].get_slice());
}

TEST_F(CelonisArrayFunctionsTest, array_lag_negative_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    offset_array->append_datum(-1L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "offset must be a positive integer.");
}

TEST_F(CelonisArrayFunctionsTest, array_lag_zero_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    offset_array->append_datum(0L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "offset must be a positive integer.");
}

TEST_F(CelonisArrayFunctionsTest, array_lag_null_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    offset_array->append_datum(kNullDatum);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "offset column must not contain null.");
}

TEST_F(CelonisArrayFunctionsTest, array_lag_input_array_contains_null) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    input_array->append_datum(kNullDatum);
    input_array->append_datum(DatumArray{"1", "2", "3"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);
    offset_array->append_datum(1L);

    const auto result = CelonisArrayFunctions::array_lag(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_TRUE(result->is_null(0));
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_EQ("1", result->get(1).get_array()[1].get_slice());
    EXPECT_EQ("2", result->get(1).get_array()[2].get_slice());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_datetime) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, false);
    input_array->append_datum(DatumArray{TimestampValue::create(2020, 8, 10, 1, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 2, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 3, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 4, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 5, 32, 32)});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(3L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(5, result->get(0).get_array().size());
    EXPECT_EQ(TimestampValue::create(2020, 8, 10, 4, 32, 32), result->get(0).get_array()[0].get_timestamp());
    EXPECT_EQ(TimestampValue::create(2020, 8, 10, 5, 32, 32), result->get(0).get_array()[1].get_timestamp());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());
    EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    EXPECT_TRUE(result->get(0).get_array()[4].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_int) {
    {
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, 3, 4, 5, 6});

        auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        offset_array->append_datum(3L);

        const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(6, result->get(0).get_array().size());
        EXPECT_EQ(4, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(5, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(6, result->get(0).get_array()[2].get_int32());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
        EXPECT_TRUE(result->get(0).get_array()[5].is_null());
    }

    {
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, kNullDatum, 4, 5, 6});

        auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        offset_array->append_datum(2L);

        const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(6, result->get(0).get_array().size());
        EXPECT_EQ(4, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(5, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(5, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(6, result->get(0).get_array()[3].get_int32());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
        EXPECT_TRUE(result->get(0).get_array()[5].is_null());
    }
}

TEST_F(CelonisArrayFunctionsTest, array_lead_large_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{1, 2, 3, 4, 5, 6});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(6L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(6, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());
    EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    EXPECT_TRUE(result->get(0).get_array()[4].is_null());
    EXPECT_TRUE(result->get(0).get_array()[5].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_varchar) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", kNullDatum, "three0", "31", "32"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(6, result->get(0).get_array().size());
    EXPECT_EQ("20", result->get(0).get_array()[0].get_slice());
    EXPECT_EQ("three0", result->get(0).get_array()[1].get_slice());
    EXPECT_EQ("three0", result->get(0).get_array()[2].get_slice());
    EXPECT_EQ("31", result->get(0).get_array()[3].get_slice());
    EXPECT_EQ("32", result->get(0).get_array()[4].get_slice());
    EXPECT_TRUE(result->get(0).get_array()[5].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_all_null) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{kNullDatum, kNullDatum, kNullDatum, kNullDatum});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(4, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());
    EXPECT_TRUE(result->get(0).get_array()[3].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_empty_column) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_empty_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"1", "2", "3"});
    input_array->append_datum(DatumArray{});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(2L);
    offset_array->append_datum(1L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ("3", result->get(0).get_array()[0].get_slice());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());
    ASSERT_EQ(0, result->get(1).get_array().size());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_input_array_contains_null) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    input_array->append_datum(kNullDatum);
    input_array->append_datum(DatumArray{"1", "2", "3"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);
    offset_array->append_datum(1L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_TRUE(result->is_null(0));
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_EQ("2", result->get(1).get_array()[0].get_slice());
    EXPECT_EQ("3", result->get(1).get_array()[1].get_slice());
    EXPECT_TRUE(result->get(1).get_array()[2].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_multiple_nulls) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", kNullDatum, "30", kNullDatum, kNullDatum, "40", "50", "60"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(2L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(1, result->size());
    ASSERT_EQ(9, result->get(0).get_array().size());
    EXPECT_EQ("30", result->get(0).get_array()[0].get_slice());
    EXPECT_EQ("40", result->get(0).get_array()[1].get_slice());
    EXPECT_EQ("40", result->get(0).get_array()[2].get_slice());
    EXPECT_EQ("50", result->get(0).get_array()[3].get_slice());
    EXPECT_EQ("50", result->get(0).get_array()[4].get_slice());
    EXPECT_EQ("50", result->get(0).get_array()[5].get_slice());
    EXPECT_EQ("60", result->get(0).get_array()[6].get_slice());
    EXPECT_TRUE(result->get(0).get_array()[7].is_null());
    EXPECT_TRUE(result->get(0).get_array()[8].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_multiple_arrays) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});
    input_array->append_datum(DatumArray{"100", "200", "300"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
    offset_array->append_datum(1L);
    offset_array->append_datum(2L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(3, result->get(0).get_array().size());
    ASSERT_EQ(3, result->get(1).get_array().size());

    EXPECT_EQ("20", result->get(0).get_array()[0].get_slice());
    EXPECT_EQ("30", result->get(0).get_array()[1].get_slice());
    EXPECT_TRUE(result->get(0).get_array()[2].is_null());

    EXPECT_EQ("300", result->get(1).get_array()[0].get_slice());
    EXPECT_TRUE(result->get(1).get_array()[1].is_null());
    EXPECT_TRUE(result->get(1).get_array()[2].is_null());
}

TEST_F(CelonisArrayFunctionsTest, array_lead_negative_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    offset_array->append_datum(-1L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "offset must be a positive integer.");
}

TEST_F(CelonisArrayFunctionsTest, array_lead_zero_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    offset_array->append_datum(0L);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "offset must be a positive integer.");
}

TEST_F(CelonisArrayFunctionsTest, array_lead_null_offset) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"10", "20", "30"});

    auto offset_array = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    offset_array->append_datum(kNullDatum);

    const auto result = CelonisArrayFunctions::array_lead(nullptr, {input_array, offset_array});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().get_error_msg(), "offset column must not contain null.");
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_int) {
    {
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 215, 225, 318, 328, 338});
        input_array->append_datum(DatumArray{1010, 1020, 2000, 2030});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 25),
                TimestampValue::create(2023, 1, 1, 0, 0, 18),
                TimestampValue::create(2023, 1, 1, 0, 0, 28),
                TimestampValue::create(2023, 1, 1, 0, 0, 38),
        });
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 0),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2, 3});
        size_array->append_datum(DatumArray{2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{11, 22, 33});
        priority_array->append_datum(DatumArray{222, 111});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(7, result->get(0).get_array().size());
        EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(215, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(318, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(120, result->get(0).get_array()[3].get_int32());
        EXPECT_EQ(225, result->get(0).get_array()[4].get_int32());
        EXPECT_EQ(328, result->get(0).get_array()[5].get_int32());
        EXPECT_EQ(338, result->get(0).get_array()[6].get_int32());
        ASSERT_EQ(4, result->get(1).get_array().size());
        EXPECT_EQ(2000, result->get(1).get_array()[0].get_int32());
        EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
        EXPECT_EQ(1020, result->get(1).get_array()[2].get_int32());
        EXPECT_EQ(2030, result->get(1).get_array()[3].get_int32());
    }
    {
        // The input_array has NULL elements.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, Datum{}, 215, 225, Datum{}, 328, 338});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 25),
                TimestampValue::create(2023, 1, 1, 0, 0, 18),
                TimestampValue::create(2023, 1, 1, 0, 0, 28),
                TimestampValue::create(2023, 1, 1, 0, 0, 38),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2, 3});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{11, 22, 33});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(7, result->get(0).get_array().size());
        EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(215, result->get(0).get_array()[1].get_int32());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
        EXPECT_EQ(225, result->get(0).get_array()[4].get_int32());
        EXPECT_EQ(328, result->get(0).get_array()[5].get_int32());
        EXPECT_EQ(338, result->get(0).get_array()[6].get_int32());
    }
    {
        // input_array is nullable.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                /*nullable=*/true);
        input_array->append_datum(DatumArray{110, 120, 215, 225, 318, 328, 338});
        input_array->append_datum(DatumArray{1010, 1020, 2000, 2030});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 25),
                TimestampValue::create(2023, 1, 1, 0, 0, 18),
                TimestampValue::create(2023, 1, 1, 0, 0, 28),
                TimestampValue::create(2023, 1, 1, 0, 0, 38),
        });
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 0),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2, 3});
        size_array->append_datum(DatumArray{2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{11, 22, 33});
        priority_array->append_datum(DatumArray{222, 111});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(7, result->get(0).get_array().size());
        EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(215, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(318, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(120, result->get(0).get_array()[3].get_int32());
        EXPECT_EQ(225, result->get(0).get_array()[4].get_int32());
        EXPECT_EQ(328, result->get(0).get_array()[5].get_int32());
        EXPECT_EQ(338, result->get(0).get_array()[6].get_int32());
        ASSERT_EQ(4, result->get(1).get_array().size());
        EXPECT_EQ(2000, result->get(1).get_array()[0].get_int32());
        EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
        EXPECT_EQ(1020, result->get(1).get_array()[2].get_int32());
        EXPECT_EQ(2030, result->get(1).get_array()[3].get_int32());
    }
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_varchar) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)),
                                                   false);
    input_array->append_datum(DatumArray{"0one", "0two", "1three", "1four", "2five"});
    input_array->append_datum(DatumArray{"0six", "0seven", "0eight", "1nine"});

    auto timestamp_array =
            ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 0),
            TimestampValue::create(2023, 1, 1, 0, 0, 40),
            TimestampValue::create(2023, 1, 1, 0, 0, 30),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 40),
            TimestampValue::create(2023, 1, 1, 0, 0, 30),
    });

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 1});
    size_array->append_datum(DatumArray{3, 1});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 2, 3});
    priority_array->append_datum(DatumArray{2, 1});

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(5, result->get(0).get_array().size());
    EXPECT_EQ("1three", result->get(0).get_array()[0].get_slice());
    EXPECT_EQ("0one", result->get(0).get_array()[1].get_slice());
    EXPECT_EQ("0two", result->get(0).get_array()[2].get_slice());
    EXPECT_EQ("2five", result->get(0).get_array()[3].get_slice());
    EXPECT_EQ("1four", result->get(0).get_array()[4].get_slice());
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_EQ("0six", result->get(1).get_array()[0].get_slice());
    EXPECT_EQ("0seven", result->get(1).get_array()[1].get_slice());
    EXPECT_EQ("1nine", result->get(1).get_array()[2].get_slice());
    EXPECT_EQ("0eight", result->get(1).get_array()[3].get_slice());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_limit_works) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), true);
    input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});
    input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto secondary_order_array = ColumnHelper::create_column(
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT)),
            false);
    secondary_order_array->append_datum(DatumArray{1L, 1L, 3L, 3L, 2L, 2L, 2L});
    secondary_order_array->append_datum(DatumArray{2L, 2L, 1L, 1L});
    secondary_order_array->append_datum(DatumArray{1L, 1L, 3L, 3L, 2L, 2L, 2L});
    secondary_order_array->append_datum(DatumArray{2L, 2L, 1L, 1L});

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 1, 1});
    priority_array->append_datum(DatumArray{1, 1});
    priority_array->append_datum(DatumArray{1, 1, 1});
    priority_array->append_datum(DatumArray{1, 1});

    auto limit_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    limit_column->append_datum(3L);
    limit_column->append_datum(kNullDatum);
    limit_column->append_datum(0L);
    limit_column->append_datum(8L);

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array, limit_column});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(4, result->size());
    // only keep 3 out of 7 elements
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(310, result->get(0).get_array()[1].get_int32());
    EXPECT_EQ(315, result->get(0).get_array()[2].get_int32());
    // limit = NULL does not change the output
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_EQ(2010, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
    EXPECT_EQ(2020, result->get(1).get_array()[2].get_int32());
    EXPECT_EQ(1020, result->get(1).get_array()[3].get_int32());
    // limit = 0
    ASSERT_EQ(0, result->get(2).get_array().size());
    // limit = 8 > length, keep all the elements
    ASSERT_EQ(4, result->get(3).get_array().size());
    EXPECT_EQ(2010, result->get(3).get_array()[0].get_int32());
    EXPECT_EQ(1010, result->get(3).get_array()[1].get_int32());
    EXPECT_EQ(2020, result->get(3).get_array()[2].get_int32());
    EXPECT_EQ(1020, result->get(3).get_array()[3].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_negative_limit) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), true);
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});
    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto secondary_order_array = ColumnHelper::create_column(
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT)),
            false);
    secondary_order_array->append_datum(DatumArray{2L, 2L, 1L, 1L});
    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2});
    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 1});
    auto limit_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    limit_column->append_datum(-5L);
    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array, limit_column});

    EXPECT_EQ(rs.status().get_error_msg(), "limit must not be negative.");
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_bigint_secondary_order) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), false);
    input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto secondary_order_array = ColumnHelper::create_column(
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT)),
            false);
    secondary_order_array->append_datum(DatumArray{1L, 1L, 3L, 3L, 2L, 2L, 2L});
    secondary_order_array->append_datum(DatumArray{2L, 2L, 1L, 1L});

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 1, 1});
    priority_array->append_datum(DatumArray{1, 1});

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(7, result->get(0).get_array().size());
    EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(310, result->get(0).get_array()[1].get_int32());
    EXPECT_EQ(315, result->get(0).get_array()[2].get_int32());
    EXPECT_EQ(215, result->get(0).get_array()[3].get_int32());
    EXPECT_EQ(120, result->get(0).get_array()[4].get_int32());
    EXPECT_EQ(320, result->get(0).get_array()[5].get_int32());
    EXPECT_EQ(225, result->get(0).get_array()[6].get_int32());
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_EQ(2010, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
    EXPECT_EQ(2020, result->get(1).get_array()[2].get_int32());
    EXPECT_EQ(1020, result->get(1).get_array()[3].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_int_secondary_order) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), false);
    input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto secondary_order_array = ColumnHelper::create_column(
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), false);
    secondary_order_array->append_datum(DatumArray{1, 1, 3, 3, 2, 2, 2});
    secondary_order_array->append_datum(DatumArray{2, 2, 1, 1});

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 1, 1});
    priority_array->append_datum(DatumArray{1, 1});

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(7, result->get(0).get_array().size());
    EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(310, result->get(0).get_array()[1].get_int32());

    EXPECT_EQ(315, result->get(0).get_array()[2].get_int32());
    EXPECT_EQ(215, result->get(0).get_array()[3].get_int32());
    EXPECT_EQ(120, result->get(0).get_array()[4].get_int32());
    EXPECT_EQ(320, result->get(0).get_array()[5].get_int32());
    EXPECT_EQ(225, result->get(0).get_array()[6].get_int32());
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_EQ(2010, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
    EXPECT_EQ(2020, result->get(1).get_array()[2].get_int32());
    EXPECT_EQ(1020, result->get(1).get_array()[3].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_null_in_input_array) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)),
                                                   false);
    input_array->append_datum(DatumArray{kNullDatum, "120", "215", "225", "310", "315", "320"});
    input_array->append_datum(DatumArray{"1010", "1020", kNullDatum, kNullDatum});

    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto secondary_order_array = ColumnHelper::create_column(
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DOUBLE)),
            false);
    secondary_order_array->append_datum(DatumArray{1.5, 1.5, 3.5, 3.5, 2.5, 2.5, 2.5});
    secondary_order_array->append_datum(DatumArray{2.5, 2.5, 1.5, 1.5});

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 1, 1});
    priority_array->append_datum(DatumArray{1, 1});

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(7, result->get(0).get_array().size());
    EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    EXPECT_EQ("310", result->get(0).get_array()[1].get_slice().to_string());

    EXPECT_EQ("315", result->get(0).get_array()[2].get_slice().to_string());
    EXPECT_EQ("215", result->get(0).get_array()[3].get_slice().to_string());
    EXPECT_EQ("120", result->get(0).get_array()[4].get_slice().to_string());
    EXPECT_EQ("320", result->get(0).get_array()[5].get_slice().to_string());
    EXPECT_EQ("225", result->get(0).get_array()[6].get_slice().to_string());
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_EQ("1010", result->get(1).get_array()[1].get_slice().to_string());
    EXPECT_TRUE(result->get(1).get_array()[2].is_null());
    EXPECT_EQ("1020", result->get(1).get_array()[3].get_slice().to_string());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_double_secondary_order) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), false);
    input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto secondary_order_array = ColumnHelper::create_column(
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DOUBLE)),
            false);
    secondary_order_array->append_datum(DatumArray{1.5, 1.5, 3.5, 3.5, 2.5, 2.5, 2.5});
    secondary_order_array->append_datum(DatumArray{2.5, 2.5, 1.5, 1.5});

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 1, 1});
    priority_array->append_datum(DatumArray{1, 1});

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(7, result->get(0).get_array().size());
    EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(310, result->get(0).get_array()[1].get_int32());

    EXPECT_EQ(315, result->get(0).get_array()[2].get_int32());
    EXPECT_EQ(215, result->get(0).get_array()[3].get_int32());
    EXPECT_EQ(120, result->get(0).get_array()[4].get_int32());
    EXPECT_EQ(320, result->get(0).get_array()[5].get_int32());
    EXPECT_EQ(225, result->get(0).get_array()[6].get_int32());
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_EQ(2010, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
    EXPECT_EQ(2020, result->get(1).get_array()[2].get_int32());
    EXPECT_EQ(1020, result->get(1).get_array()[3].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_string_secondary_order) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), false);
    input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto secondary_order_array = ColumnHelper::create_column(
            TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)), false);
    secondary_order_array->append_datum(DatumArray{"apple", "apple", "cat", "cat", "bus", "bus", "bus"});
    secondary_order_array->append_datum(DatumArray{"bus", "bus", "apple", "apple"});

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{1, 1, 1});
    priority_array->append_datum(DatumArray{1, 1});

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(7, result->get(0).get_array().size());
    EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(310, result->get(0).get_array()[1].get_int32());

    EXPECT_EQ(315, result->get(0).get_array()[2].get_int32());
    EXPECT_EQ(215, result->get(0).get_array()[3].get_int32());
    EXPECT_EQ(120, result->get(0).get_array()[4].get_int32());
    EXPECT_EQ(320, result->get(0).get_array()[5].get_int32());
    EXPECT_EQ(225, result->get(0).get_array()[6].get_int32());
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_EQ(2010, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
    EXPECT_EQ(2020, result->get(1).get_array()[2].get_int32());
    EXPECT_EQ(1020, result->get(1).get_array()[3].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_priority) {
    auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)), false);
    input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});
    input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

    auto timestamp_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)),
                                                       false);
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 25),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 15),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });
    timestamp_array->append_datum(DatumArray{
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
            TimestampValue::create(2023, 1, 1, 0, 0, 10),
            TimestampValue::create(2023, 1, 1, 0, 0, 20),
    });

    auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                  false);
    size_array->append_datum(DatumArray{2, 2, 3});
    size_array->append_datum(DatumArray{2, 2});

    auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
    priority_array->append_datum(DatumArray{3, 1, 2});
    priority_array->append_datum(DatumArray{1, 2});

    const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
            nullptr, {input_array, timestamp_array, size_array, priority_array});
    ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
    const auto& result = rs.value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(7, result->get(0).get_array().size());
    EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(310, result->get(0).get_array()[1].get_int32());
    EXPECT_EQ(315, result->get(0).get_array()[2].get_int32());
    EXPECT_EQ(215, result->get(0).get_array()[3].get_int32());
    EXPECT_EQ(120, result->get(0).get_array()[4].get_int32());
    EXPECT_EQ(320, result->get(0).get_array()[5].get_int32());
    EXPECT_EQ(225, result->get(0).get_array()[6].get_int32());
    ASSERT_EQ(4, result->get(1).get_array().size());
    EXPECT_EQ(2010, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(1010, result->get(1).get_array()[1].get_int32());
    EXPECT_EQ(2020, result->get(1).get_array()[2].get_int32());
    EXPECT_EQ(1020, result->get(1).get_array()[3].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_null_array) {
    {
        // timestamp_array is NULL.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 310, 315, 320});
        input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), true);
        timestamp_array->append_datum(Datum{});
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 3});
        size_array->append_datum(DatumArray{2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{2, 1});
        priority_array->append_datum(DatumArray{1, 2});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "timestamp_array should not be NULL.");
    }
    {
        // priority_array is NULL.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), true);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          true);
        priority_array->append_datum(Datum{});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "priority_array should not be NULL.");
    }
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_null_elements) {
    {
        // timestamp_array has NULL elements.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{Datum{}, 310, 315, 320});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                Datum{},
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{1, 3});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{1, 2});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "timestamp_array should not have NULL elements.");
    }
    {
        // size_array has NULL elements.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{Datum{}, 310, 315, 320});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{1, Datum{}});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{1, 2});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "size_array should not have NULL elements.");
    }
    {
        // priority_array has NULL elements.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{1, Datum{}});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "priority_array should not have NULL elements.");
    }
    {
        // secondary_order_array has NULL elements
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 310, 315, 320});
        input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), true);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
                TimestampValue::create(2023, 1, 1, 0, 0, 40),
                TimestampValue::create(2023, 1, 1, 0, 0, 50),
        });
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
        });

        auto secondary_order_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT)), true);
        secondary_order_array->append_datum(DatumArray{1L, 2L, 3L, 4L, 5L});
        secondary_order_array->append_datum(DatumArray{1L, 2L, kNullDatum, 4L});

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 3});
        size_array->append_datum(DatumArray{2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{2, 1});
        priority_array->append_datum(DatumArray{1, 2});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "If provided, secondary_order_array should not have NULL elements.");
    }
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_array_size_mismatch) {
    {
        // timestamp_array has a different size than input_array.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 25),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2, 3});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{3, 1, 2});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "The size of input_array and timestamp_array should not be different.");
    }
    {
        // secondary_order_array has a different size than input_array.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 310, 315, 320});
        input_array->append_datum(DatumArray{1010, 1020, 2010, 2020});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), true);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
                TimestampValue::create(2023, 1, 1, 0, 0, 40),
                TimestampValue::create(2023, 1, 1, 0, 0, 50),
        });
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
        });

        auto secondary_order_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT)), true);
        secondary_order_array->append_datum(DatumArray{1L, 2L, 3L, 4L, 5L});
        secondary_order_array->append_datum(DatumArray{1L, 2L, 4L});

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 3});
        size_array->append_datum(DatumArray{2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{2, 1});
        priority_array->append_datum(DatumArray{1, 2});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array, secondary_order_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "If provided, the size of secondary_order_array and timestamp_array should not be different.");
    }
    {
        // timestamp_array and input_array has a different size than the sum of size_array.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 25),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2, 2});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{3, 1, 2});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "The size of input_array and timestamp_array should not be different than the sum of size_array.");
    }
    {
        // priority_array has a different size than size_array.
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 215, 225, 310, 315, 320});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 25),
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
        });

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2, 3});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{3, 1, 2, 4});

        const auto result = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(),
                  "The size of size_array and priority_array should not be different.");
    }
}

TEST_F(CelonisArrayFunctionsTest, merge_sorted_arrays_empty_arrays) {
    {
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                       false);
        input_array->append_datum(DatumArray{110, 120, 215, 225, 318, 328, 338});
        input_array->append_datum(DatumArray{});
        input_array->append_datum(DatumArray{});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 15),
                TimestampValue::create(2023, 1, 1, 0, 0, 25),
                TimestampValue::create(2023, 1, 1, 0, 0, 18),
                TimestampValue::create(2023, 1, 1, 0, 0, 28),
                TimestampValue::create(2023, 1, 1, 0, 0, 38),
        });
        timestamp_array->append_datum(DatumArray{});
        timestamp_array->append_datum(DatumArray{});

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{2, 2, 3, 0});
        size_array->append_datum(DatumArray{0, 0});
        size_array->append_datum(DatumArray{0});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{11, 22, 33, 44});
        priority_array->append_datum(DatumArray{222, 111});
        priority_array->append_datum(DatumArray{111});

        const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
        const auto& result = rs.value();
        ASSERT_EQ(3, result->size());
        ASSERT_EQ(7, result->get(0).get_array().size());
        EXPECT_EQ(110, result->get(0).get_array()[0].get_int32());
        EXPECT_EQ(215, result->get(0).get_array()[1].get_int32());
        EXPECT_EQ(318, result->get(0).get_array()[2].get_int32());
        EXPECT_EQ(120, result->get(0).get_array()[3].get_int32());
        EXPECT_EQ(225, result->get(0).get_array()[4].get_int32());
        EXPECT_EQ(328, result->get(0).get_array()[5].get_int32());
        EXPECT_EQ(338, result->get(0).get_array()[6].get_int32());
        EXPECT_EQ(0, result->get(1).get_array().size());
        EXPECT_EQ(0, result->get(2).get_array().size());
    }
    {
        auto input_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR)),
                                                       false);
        input_array->append_datum(DatumArray{"0one", "0two", "1three", "1four", "2five"});
        input_array->append_datum(DatumArray{"0six", "0seven", "0eight", "1nine"});
        input_array->append_datum(DatumArray{});

        auto timestamp_array =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME)), false);
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 0),
                TimestampValue::create(2023, 1, 1, 0, 0, 40),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
        });
        timestamp_array->append_datum(DatumArray{
                TimestampValue::create(2023, 1, 1, 0, 0, 10),
                TimestampValue::create(2023, 1, 1, 0, 0, 20),
                TimestampValue::create(2023, 1, 1, 0, 0, 40),
                TimestampValue::create(2023, 1, 1, 0, 0, 30),
        });
        timestamp_array->append_datum(DatumArray{});

        auto size_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                      false);
        size_array->append_datum(DatumArray{0, 2, 2, 1, 0, 0});
        size_array->append_datum(DatumArray{3, 0, 0, 1});
        size_array->append_datum(DatumArray{0, 0});

        auto priority_array = ColumnHelper::create_column(TypeDescriptor::create_array_type(TypeDescriptor(TYPE_INT)),
                                                          false);
        priority_array->append_datum(DatumArray{1, 2, 3, 4, 5, 6});
        priority_array->append_datum(DatumArray{4, 3, 2, 1});
        priority_array->append_datum(DatumArray{2, 1});

        const auto rs = CelonisArrayFunctions::merge_sorted_arrays(
                nullptr, {input_array, timestamp_array, size_array, priority_array});
        ASSERT_TRUE(rs.ok()) << rs.status().get_error_msg();
        const auto& result = rs.value();
        ASSERT_EQ(3, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_EQ("1three", result->get(0).get_array()[0].get_slice());
        EXPECT_EQ("0one", result->get(0).get_array()[1].get_slice());
        EXPECT_EQ("0two", result->get(0).get_array()[2].get_slice());
        EXPECT_EQ("2five", result->get(0).get_array()[3].get_slice());
        EXPECT_EQ("1four", result->get(0).get_array()[4].get_slice());
        ASSERT_EQ(4, result->get(1).get_array().size());
        EXPECT_EQ("0six", result->get(1).get_array()[0].get_slice());
        EXPECT_EQ("0seven", result->get(1).get_array()[1].get_slice());
        EXPECT_EQ("1nine", result->get(1).get_array()[2].get_slice());
        EXPECT_EQ("0eight", result->get(1).get_array()[3].get_slice());
        EXPECT_EQ(0, result->get(2).get_array().size());
    }
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_empty_input_array_column) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_empty_input_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{});
    input_array->append_datum(DatumArray{3, 2});

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(0, result->get(0).get_array().size());
    ASSERT_EQ(2, result->get(1).get_array().size());
    EXPECT_EQ(3, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(2, result->get(1).get_array()[1].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_null_input_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    input_array->append_datum(Datum{});
    input_array->append_datum(DatumArray{3, 2});
    input_array->append_datum(Datum{});

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(3, result->size());
    ASSERT_EQ(0, result->get(0).get_array().size());
    ASSERT_EQ(2, result->get(1).get_array().size());
    EXPECT_EQ(3, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(2, result->get(1).get_array()[1].get_int32());
    ASSERT_EQ(0, result->get(2).get_array().size());
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_const_null_input_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    input_array->append_datum(Datum{});
    input_array->append_datum(Datum{});
    input_array->append_datum(Datum{});

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(3, result->size());
    ASSERT_EQ(0, result->get(0).get_array().size());
    ASSERT_EQ(0, result->get(1).get_array().size());
    ASSERT_EQ(0, result->get(2).get_array().size());
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_nonnull_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{1, 2, 3});
    input_array->append_datum(DatumArray{3, 2});

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(1, result->get(0).get_array()[0].get_int32());
    EXPECT_EQ(2, result->get(0).get_array()[1].get_int32());
    EXPECT_EQ(3, result->get(0).get_array()[2].get_int32());
    ASSERT_EQ(2, result->get(1).get_array().size());
    EXPECT_EQ(3, result->get(1).get_array()[0].get_int32());
    EXPECT_EQ(2, result->get(1).get_array()[1].get_int32());
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_datetime) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
    input_array->append_datum(DatumArray{TimestampValue::create(2020, 8, 10, 1, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 2, 32, 32),
                                         TimestampValue::create(2020, 8, 10, 3, 32, 32)});
    input_array->append_datum(Datum());

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(2, result->size());
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(TimestampValue::create(2020, 8, 10, 1, 32, 32), result->get(0).get_array()[0].get_timestamp());
    EXPECT_EQ(TimestampValue::create(2020, 8, 10, 2, 32, 32), result->get(0).get_array()[1].get_timestamp());
    EXPECT_EQ(TimestampValue::create(2020, 8, 10, 3, 32, 32), result->get(0).get_array()[2].get_timestamp());
    ASSERT_EQ(0, result->get(1).get_array().size());
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_varchar) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    input_array->append_datum(DatumArray{"1", "2"});
    input_array->append_datum(DatumArray{});
    input_array->append_datum(Datum());

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(3, result->size());
    ASSERT_EQ(2, result->get(0).get_array().size());
    EXPECT_EQ("1", result->get(0).get_array()[0].get_slice());
    EXPECT_EQ("2", result->get(0).get_array()[1].get_slice());
    ASSERT_EQ(0, result->get(1).get_array().size());
    ASSERT_EQ(0, result->get(2).get_array().size());
}

TEST_F(CelonisArrayFunctionsTest, null_to_empty_null_elements_in_array) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    input_array->append_datum(DatumArray{"1", "2"});
    input_array->append_datum(DatumArray{kNullDatum, kNullDatum});
    input_array->append_datum(DatumArray{kNullDatum, "HELLO"});
    input_array->append_datum(DatumArray{kNullDatum});

    const auto result = CelonisArrayFunctions::null_to_empty(nullptr, {input_array}).value();
    ASSERT_EQ(4, result->size());
    ASSERT_EQ(2, result->get(0).get_array().size());
    EXPECT_EQ("1", result->get(0).get_array()[0].get_slice());
    EXPECT_EQ("2", result->get(0).get_array()[1].get_slice());
    ASSERT_EQ(2, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_TRUE(result->get(1).get_array()[1].is_null());
    ASSERT_EQ(2, result->get(2).get_array().size());
    EXPECT_TRUE(result->get(2).get_array()[0].is_null());
    EXPECT_EQ("HELLO", result->get(2).get_array()[1].get_slice());
    ASSERT_EQ(1, result->get(3).get_array().size());
    EXPECT_TRUE(result->get(3).get_array()[0].is_null());
}

TEST_F(CelonisArrayFunctionsTest, calc_crop_normal_cases) {
    // empty input
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        EXPECT_EQ(0, result->size());
    }
    // empty activity array
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(0, result->get(0).get_array().size());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(
                DatumArray{kNullDatum, "B", kNullDatum, "C", "C", kNullDatum, "L", "C", "D", kNullDatum});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(10, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        for (size_t i = 1; i <= 7; ++i) {
            EXPECT_EQ(1, result->get(0).get_array()[i].get_int64());
        }
        EXPECT_TRUE(result->get(0).get_array()[8].is_null());
        EXPECT_TRUE(result->get(0).get_array()[9].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "D"});
        activity->append_datum(DatumArray{"A", "B", "D", "E"});
        begin_activity->append_datum("B");
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        ASSERT_EQ(4, result->get(1).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_EQ(1, result->get(0).get_array()[1].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int64());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
        EXPECT_TRUE(result->get(1).get_array()[0].is_null());
        EXPECT_TRUE(result->get(1).get_array()[1].is_null());
        EXPECT_TRUE(result->get(1).get_array()[2].is_null());
        EXPECT_TRUE(result->get(1).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "D"});
        begin_activity->append_datum("C");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("B");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "A", "B", "B"});
        begin_activity->append_datum("A");
        begin_mode->append_datum("LAST");
        end_activity->append_datum("B");
        end_mode->append_datum("FIRST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_EQ(1, result->get(0).get_array()[1].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int64());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "A", "B", "B"});
        begin_activity->append_datum("A");
        begin_mode->append_datum("LAST");
        end_activity->append_datum("X");
        end_mode->append_datum("FIRST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum("A");
        begin_mode->append_datum("ALL");
        end_activity->append_datum("B");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_EQ(1, result->get(0).get_array()[0].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[1].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[3].get_int64());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_EQ(1, result->get(0).get_array()[0].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[1].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[3].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[4].get_int64());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum("X");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum("C");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[3].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[4].get_int64());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{kNullDatum, "B", kNullDatum, "B", kNullDatum});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_EQ(1, result->get(0).get_array()[0].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[1].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[3].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[4].get_int64());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode,
                                                              end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(0, result->get(0).get_array().size());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{kNullDatum});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode,
                                                              end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(1, result->get(0).get_array().size());
        EXPECT_EQ(1L, result->get(0).get_array()[0].get_int64());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", kNullDatum, "C"});
        activity->append_datum(DatumArray{});
        begin_activity->append_datum(kNullDatum);
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode,
                                                              end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(3, result->get(0).get_array().size());
        ASSERT_EQ(0, result->get(1).get_array().size());
        EXPECT_EQ(1, result->get(0).get_array()[0].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[1].get_int64());
        EXPECT_EQ(1, result->get(0).get_array()[2].get_int64());
    }
}

TEST_F(CelonisArrayFunctionsTest, calc_crop_null_input) {
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(kNullDatum);
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum(kNullDatum);
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum(kNullDatum);

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisArrayFunctionsTest, calc_crop_invalid_input) {
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("UNKNOWN");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "begin range mode must be FIRST/LAST/ALL.");
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("UNKNOWN");

        const auto result = CelonisArrayFunctions::calc_crop(nullptr,
                                                             {activity, begin_activity, begin_mode, end_activity,
                                                              end_mode});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "end range mode must be FIRST/LAST/ALL.");
    }
}

TEST_F(CelonisArrayFunctionsTest, calc_crop_to_null_normal_cases) {
    // empty input
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        EXPECT_EQ(0, result->size());
    }
    // empty activity array
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ(0, result->get(0).get_array().size());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(
                DatumArray{kNullDatum, "B", kNullDatum, "C", kNullDatum, "C", "L", "C", "D", kNullDatum});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(10, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_EQ("B", result->get(0).get_array()[1].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_EQ("C", result->get(0).get_array()[3].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
        EXPECT_EQ("C", result->get(0).get_array()[5].get_slice());
        EXPECT_EQ("L", result->get(0).get_array()[6].get_slice());
        EXPECT_EQ("C", result->get(0).get_array()[7].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[8].is_null());
        EXPECT_TRUE(result->get(0).get_array()[9].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "D"});
        activity->append_datum(DatumArray{"A", "B", "D", "E"});
        begin_activity->append_datum("B");
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        ASSERT_EQ(4, result->get(1).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_EQ("B", result->get(0).get_array()[1].get_slice());
        EXPECT_EQ("C", result->get(0).get_array()[2].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
        EXPECT_TRUE(result->get(1).get_array()[0].is_null());
        EXPECT_TRUE(result->get(1).get_array()[1].is_null());
        EXPECT_TRUE(result->get(1).get_array()[2].is_null());
        EXPECT_TRUE(result->get(1).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "D"});
        begin_activity->append_datum("C");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("B");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "A", "B", "B"});
        begin_activity->append_datum("A");
        begin_mode->append_datum("LAST");
        end_activity->append_datum("B");
        end_mode->append_datum("FIRST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_EQ("A", result->get(0).get_array()[1].get_slice());
        EXPECT_EQ("B", result->get(0).get_array()[2].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "A", "B", "B"});
        begin_activity->append_datum("A");
        begin_mode->append_datum("LAST");
        end_activity->append_datum("X");
        end_mode->append_datum("FIRST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(4, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum("A");
        begin_mode->append_datum("ALL");
        end_activity->append_datum("B");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_EQ("A", result->get(0).get_array()[0].get_slice());
        EXPECT_EQ("B", result->get(0).get_array()[1].get_slice());
        EXPECT_EQ("C", result->get(0).get_array()[2].get_slice());
        EXPECT_EQ("B", result->get(0).get_array()[3].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_EQ("A", result->get(0).get_array()[0].get_slice());
        EXPECT_EQ("B", result->get(0).get_array()[1].get_slice());
        EXPECT_EQ("C", result->get(0).get_array()[2].get_slice());
        EXPECT_EQ("B", result->get(0).get_array()[3].get_slice());
        EXPECT_EQ("D", result->get(0).get_array()[4].get_slice());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum("X");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_TRUE(result->get(0).get_array()[3].is_null());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", "B", "C", "B", "D"});
        begin_activity->append_datum("C");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_EQ("C", result->get(0).get_array()[2].get_slice());
        EXPECT_EQ("B", result->get(0).get_array()[3].get_slice());
        EXPECT_EQ("D", result->get(0).get_array()[4].get_slice());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{kNullDatum, "B", kNullDatum, "C", kNullDatum});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(5, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
        EXPECT_EQ("B", result->get(0).get_array()[1].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[2].is_null());
        EXPECT_EQ("C", result->get(0).get_array()[3].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[4].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(0, result->get(0).get_array().size());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{kNullDatum});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(1, result->get(0).get_array().size());
        EXPECT_TRUE(result->get(0).get_array()[0].is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"A", kNullDatum, "C"});
        activity->append_datum(DatumArray{});
        begin_activity->append_datum(kNullDatum);
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("ALL");
        begin_mode->append_datum("ALL");
        end_activity->append_datum(kNullDatum);
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("ALL");
        end_mode->append_datum("ALL");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(2, result->size());
        ASSERT_EQ(3, result->get(0).get_array().size());
        ASSERT_EQ(0, result->get(1).get_array().size());
        EXPECT_EQ("A", result->get(0).get_array()[0].get_slice());
        EXPECT_TRUE(result->get(0).get_array()[1].is_null());
        EXPECT_EQ("C", result->get(0).get_array()[2].get_slice());
    }
}

TEST_F(CelonisArrayFunctionsTest, calc_crop_to_null_null_input) {
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(kNullDatum);
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum(kNullDatum);
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum(kNullDatum);
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum(kNullDatum);
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum(kNullDatum);

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

TEST_F(CelonisArrayFunctionsTest, calc_crop_to_null_invalid_input) {
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("UNKNOWN");
        end_activity->append_datum("C");
        end_mode->append_datum("LAST");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "begin range mode must be FIRST/LAST/ALL.");
    }
    {
        auto activity = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        auto begin_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto begin_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto end_mode = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        activity->append_datum(DatumArray{"B", "C"});
        begin_activity->append_datum("B");
        begin_mode->append_datum("FIRST");
        end_activity->append_datum("C");
        end_mode->append_datum("UNKNOWN");

        const auto result = CelonisArrayFunctions::calc_crop_to_null(nullptr,
                                                                     {activity, begin_activity, begin_mode,
                                                                      end_activity,
                                                                      end_mode});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "end range mode must be FIRST/LAST/ALL.");
    }
}

TEST_F(CelonisArrayFunctionsTest, array_count_const_null_column) {
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        arrays->append_datum(kNullDatum);
        const auto result = CelonisArrayFunctions::array_count(nullptr, {ConstColumn::create(arrays, 2)}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto arrays = ColumnHelper::create_const_null_column(2);
        const auto result = CelonisArrayFunctions::array_count(nullptr, {arrays}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisArrayFunctionsTest, array_count_normal_case) {
    // STRING
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        arrays->append_datum(DatumArray{});
        arrays->append_datum(kNullDatum);
        arrays->append_datum(DatumArray{kNullDatum, "A", kNullDatum, "B"});
        arrays->append_datum(DatumArray{kNullDatum, "A", kNullDatum, "B", kNullDatum, "A"});
        arrays->append_datum(DatumArray{kNullDatum});
        arrays->append_datum(DatumArray{"A", "A"});
        arrays->append_datum(DatumArray{"A", "C", "D"});
        const auto result = CelonisArrayFunctions::array_count(nullptr, {arrays}).value();
        EXPECT_EQ(7, result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(2L, result->get(2).get_int64());
        EXPECT_EQ(3L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(2L, result->get(5).get_int64());
        EXPECT_EQ(3L, result->get(6).get_int64());
    }
    // INT
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
        arrays->append_datum(DatumArray{});
        arrays->append_datum(kNullDatum);
        arrays->append_datum(DatumArray{kNullDatum, 1, kNullDatum, 2});
        arrays->append_datum(DatumArray{kNullDatum, 1, kNullDatum, 2, kNullDatum, 2});
        arrays->append_datum(DatumArray{kNullDatum});
        arrays->append_datum(DatumArray{1, 2, 1, 2});
        arrays->append_datum(DatumArray{1, 3, 4});
        const auto result = CelonisArrayFunctions::array_count(nullptr, {arrays}).value();
        EXPECT_EQ(7, result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(2L, result->get(2).get_int64());
        EXPECT_EQ(3L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(4L, result->get(5).get_int64());
        EXPECT_EQ(3L, result->get(6).get_int64());
    }
    // BIGINT
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
        arrays->append_datum(DatumArray{});
        arrays->append_datum(kNullDatum);
        arrays->append_datum(DatumArray{kNullDatum, 1L, kNullDatum, 2L});
        arrays->append_datum(DatumArray{kNullDatum, 1L, kNullDatum, 2L, kNullDatum});
        arrays->append_datum(DatumArray{kNullDatum});
        arrays->append_datum(DatumArray{1L, 2L, 1L, 2L});
        arrays->append_datum(DatumArray{1L, 3L, 4L});
        const auto result = CelonisArrayFunctions::array_count(nullptr, {arrays}).value();
        EXPECT_EQ(7, result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(2L, result->get(2).get_int64());
        EXPECT_EQ(2L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(4L, result->get(5).get_int64());
        EXPECT_EQ(3L, result->get(6).get_int64());
    }
    // DOUBLE
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, true);
        arrays->append_datum(DatumArray{});
        arrays->append_datum(kNullDatum);
        arrays->append_datum(DatumArray{kNullDatum, 1.5, kNullDatum, 2.5, 1.5});
        arrays->append_datum(DatumArray{kNullDatum, 1.5, kNullDatum, 2.5, kNullDatum});
        arrays->append_datum(DatumArray{kNullDatum});
        arrays->append_datum(DatumArray{1.5, 1.5});
        arrays->append_datum(DatumArray{1.5, 3.5, 4.5});
        const auto result = CelonisArrayFunctions::array_count(nullptr, {arrays}).value();
        EXPECT_EQ(7, result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(3L, result->get(2).get_int64());
        EXPECT_EQ(2L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(2L, result->get(5).get_int64());
        EXPECT_EQ(3L, result->get(6).get_int64());
    }
    // DATETIME
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
        arrays->append_datum(DatumArray{});
        arrays->append_datum(kNullDatum);
        arrays->append_datum(DatumArray{kNullDatum, TimestampValue::create(1970, 1, 1, 0, 0, 0), kNullDatum,
                                        TimestampValue::create(1970, 1, 1, 0, 0, 1)});
        arrays->append_datum(DatumArray{kNullDatum, TimestampValue::create(1970, 1, 1, 0, 0, 0), kNullDatum,
                                        TimestampValue::create(1971, 1, 1, 0, 0, 0), kNullDatum});
        arrays->append_datum(DatumArray{kNullDatum});
        arrays->append_datum(
                DatumArray{TimestampValue::create(1971, 1, 1, 0, 0, 0), TimestampValue::create(1971, 1, 1, 0, 0, 0)});
        arrays->append_datum(
                DatumArray{
                        TimestampValue::create(1973, 1, 1, 0, 0, 0), TimestampValue::create(1975, 1, 1, 0, 0, 0),
                        TimestampValue::create(1977, 1, 1, 0, 0, 0)
                });
        const auto result = CelonisArrayFunctions::array_count(nullptr, {arrays}).value();
        EXPECT_EQ(7, result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(2L, result->get(2).get_int64());
        EXPECT_EQ(2L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(2L, result->get(5).get_int64());
        EXPECT_EQ(3L, result->get(6).get_int64());
    }
}

TEST_F(CelonisArrayFunctionsTest, array_bool_or_const_null_column) {
    {
        auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BOOLEAN, true);
        arrays->append_datum(kNullDatum);
        const auto result = CelonisArrayFunctions::array_bool_or(nullptr, {ConstColumn::create(arrays, 2)}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
    {
        auto arrays = ColumnHelper::create_const_null_column(2);
        const auto result = CelonisArrayFunctions::array_bool_or(nullptr, {arrays}).value();
        EXPECT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisArrayFunctionsTest, array_bool_or_normal_case) {
    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BOOLEAN, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, false, kNullDatum, false});
    arrays->append_datum(DatumArray{kNullDatum, false, kNullDatum, true, kNullDatum, true});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{false, false, true});
    arrays->append_datum(DatumArray{true, true});
    arrays->append_datum(DatumArray{false});
    const auto result = CelonisArrayFunctions::array_bool_or(nullptr, {arrays}).value();
    EXPECT_EQ(8, result->size());
    EXPECT_EQ(0, result->get(0).get_uint8());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(0, result->get(2).get_uint8());
    EXPECT_EQ(1, result->get(3).get_uint8());
    EXPECT_EQ(0, result->get(4).get_uint8());
    EXPECT_EQ(1, result->get(5).get_uint8());
    EXPECT_EQ(1, result->get(6).get_uint8());
    EXPECT_EQ(0, result->get(7).get_uint8());
}

} // namespace starrocks
