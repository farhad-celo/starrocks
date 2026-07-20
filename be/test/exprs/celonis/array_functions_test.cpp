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

} // namespace starrocks
