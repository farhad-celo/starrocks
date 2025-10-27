#include "exprs/celonis/shortened_variant.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "util.h"

namespace starrocks {

class CelonisShortenedVariantTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
};

TEST_F(CelonisShortenedVariantTest, array_celonis_shortened_has_cycle) {
    auto array_column = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    // Input data:
    array_column->append_datum(DatumArray{"a", "b", "b", "b", "c"});
    array_column->append_datum(DatumArray{"a", "b"});
    array_column->append_datum(DatumArray{"b", "b"});
    array_column->append_datum(DatumArray{"b"});
    array_column->append_datum(DatumArray{"b", "b", "b", "a", "d", "d"});

    auto max_cycle_length_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false, true, 0);
    max_cycle_length_column->append_datum(2L);

    const auto result_sources =
            CelonisShortenedVariant::celonis_shortened_variant(nullptr, {array_column, max_cycle_length_column})
                    .value();
    ASSERT_EQ(5, result_sources->size());
    auto first_row = result_sources->get(0).get_array();
    EXPECT_EQ(4, first_row.size());
    EXPECT_EQ("a", first_row[0].get_slice());
    EXPECT_EQ("b", first_row[1].get_slice());
    EXPECT_EQ("b", first_row[2].get_slice());
    EXPECT_EQ("c", first_row[3].get_slice());

    auto second_row = result_sources->get(1).get_array();
    EXPECT_EQ(2, second_row.size());
    EXPECT_EQ("a", second_row[0].get_slice());
    EXPECT_EQ("b", second_row[1].get_slice());

    auto third_row = result_sources->get(2).get_array();
    EXPECT_EQ(2, third_row.size());
    EXPECT_EQ("b", third_row[0].get_slice());
    EXPECT_EQ("b", third_row[1].get_slice());

    auto fourth_row = result_sources->get(3).get_array();
    EXPECT_EQ(1, fourth_row.size());
    EXPECT_EQ("b", fourth_row[0].get_slice());

    auto fifth_row = result_sources->get(4).get_array();
    EXPECT_EQ(5, fifth_row.size());
    EXPECT_EQ("b", fifth_row[0].get_slice());
    EXPECT_EQ("b", fifth_row[1].get_slice());
    EXPECT_EQ("a", fifth_row[2].get_slice());
    EXPECT_EQ("d", fifth_row[3].get_slice());
    EXPECT_EQ("d", fifth_row[4].get_slice());
}

TEST_F(CelonisShortenedVariantTest, array_celonis_shortened_null_arrays) {
    auto array_column = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    array_column->append_datum(DatumArray{Datum()});
    array_column->append_datum(Datum());
    array_column->append_datum(DatumArray{Datum(), "b", Datum(), "b", "b"});
    array_column->append_datum(DatumArray{"b", Datum(), "b", Datum(), "b"});
    auto max_cycle_length_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false, true, 0);
    max_cycle_length_column->append_datum(2L);
    const auto result_sources =
            CelonisShortenedVariant::celonis_shortened_variant(nullptr, {array_column, max_cycle_length_column})
                    .value();
    ASSERT_EQ(4, result_sources->size());
    auto first_row = result_sources->get(0).get_array();
    EXPECT_EQ(0, first_row.size());

    EXPECT_TRUE(result_sources->get(1).is_null());

    auto third_row = result_sources->get(2).get_array();
    EXPECT_EQ(2, third_row.size());
    EXPECT_EQ("b", third_row[0].get_slice());
    EXPECT_EQ("b", third_row[1].get_slice());

    auto fourth_row = result_sources->get(3).get_array();
    EXPECT_EQ(2, fourth_row.size());
    EXPECT_EQ("b", fourth_row[0].get_slice());
    EXPECT_EQ("b", fourth_row[1].get_slice());
}

TEST_F(CelonisShortenedVariantTest, array_celonis_shortened_longer_cycle) {
    auto array_column = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    // Input data:
    array_column->append_datum(DatumArray{"b", "b", "b", "b", "c", "c", "c"});
    auto max_cycle_length_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false, true, 0);
    max_cycle_length_column->append_datum(3L);

    const auto result_sources =
            CelonisShortenedVariant::celonis_shortened_variant(nullptr, {array_column, max_cycle_length_column})
                    .value();
    ASSERT_EQ(1, result_sources->size());
    auto first_row = result_sources->get(0).get_array();
    EXPECT_EQ(6, first_row.size());
    EXPECT_EQ("b", first_row[0].get_slice());
    EXPECT_EQ("b", first_row[1].get_slice());
    EXPECT_EQ("b", first_row[2].get_slice());
    EXPECT_EQ("c", first_row[3].get_slice());
    EXPECT_EQ("c", first_row[4].get_slice());
    EXPECT_EQ("c", first_row[5].get_slice());
}
} // namespace starrocks
