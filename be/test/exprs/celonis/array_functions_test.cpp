#include "exprs/celonis/array_functions.h"

#include "column/column_helper.h"
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
        // input_array has NULL.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
        input_array->append_datum(Datum{});
        input_array->append_datum(DatumArray{1, 2, 3, 4});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        key_array->append_datum(DatumArray{""});
        key_array->append_datum(DatumArray{"e1", "e2", "e3", "e4"});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "input_array should not be null.");
    }
    {
        // key_array has NULL.
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{1, 2, 3, 4});
        input_array->append_datum(DatumArray{1, 2, 3, 4});

        auto key_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        key_array->append_datum(DatumArray{"e1", "e2", "e3", "e4"});
        key_array->append_datum(Datum{});

        const auto result = CelonisArrayFunctions::dedup_sorted_by(nullptr, {input_array, key_array});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().get_error_msg(), "key_array should not be null.");
    }
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
        // timestamp_array has NULL.
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
        // priority_array has NULL.
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

} // namespace starrocks
