#include "exprs/celonis/result_table.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"

namespace starrocks {

class CelonisResultTableTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(CelonisResultTableTest, existing_column) {
    // GIVEN
    celonis::ResultTable table{"table", 0};
    const std::string column_name{"col"};
    const auto& expected{table.AddColumn<int>(column_name)};

    // WHEN
    const auto& actual{table.column<int>(column_name)};

    // THEN expect that they point to the same address
    EXPECT_EQ(&expected, &actual);
}

TEST_F(CelonisResultTableTest, existing_nullable_column) {
    // GIVEN
    celonis::ResultTable table{"table", 0};
    const std::string column_name{"col"};
    const auto& expected{table.AddNullableColumn<int>(column_name)};

    // WHEN
    const auto& actual{table.nullable_column<int>(column_name)};

    // THEN expect that they point to the same address
    EXPECT_EQ(&expected, &actual);
}

TEST_F(CelonisResultTableTest, existing_nullable_column_as_column) {
    // GIVEN
    celonis::ResultTable table{"table", 0};
    const std::string column_name{"col"};
    const auto& expected_nullable_column{table.AddNullableColumn<int>(column_name)};

    // WHEN
    const auto& actual_column{table.column<int>(column_name)};

    // THEN expect that they point to the same address
    EXPECT_EQ(&expected_nullable_column, &actual_column);
}

TEST_F(CelonisResultTableTest, existing_column_as_nullable_column) {
    // GIVEN
    celonis::ResultTable table{"table", 0};
    const std::string column_name{"col"};
    const auto& expected_nullable_column{table.AddColumn<int>(column_name)};

    // WHEN we try to access a non nullable column as nullable column
    // THEN  we should get an cast exception
    EXPECT_THROW(table.nullable_column<int>(column_name), std::bad_cast);
}

TEST_F(CelonisResultTableTest, non_existing_column) {
    // GIVEN
    celonis::ResultTable table{"table", 0};
    const auto& expected_nullable_column{table.AddColumn<int>("col")};

    // WHEN we try to access it
    // THEN we throw
    EXPECT_THROW(table.column<int>("non_existing_column"), std::out_of_range);
}

TEST_F(CelonisResultTableTest, non_existing_nullable_column) {
    // GIVEN
    celonis::ResultTable table{"table", 0};
    const auto& expected_nullable_column{table.AddColumn<int>("col")};

    // WHEN we try to access it
    // THEN we throw
    EXPECT_THROW(table.nullable_column<int>("non_existing_column"), std::out_of_range);
}

} // namespace starrocks
