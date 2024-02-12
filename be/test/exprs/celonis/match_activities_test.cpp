#include "exprs/celonis/match_activities.h"

#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "util.h"
#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisMatchActivitiesTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    std::shared_ptr<Column> create_const_filter(const DatumArray& array, int size) {
        auto result = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        for (int i = 0; i < size; ++i) {
            result->append_datum(array);
        }
        return result;
    }

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
};

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_string_data) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    // Input data:
    //  row 0 ["string1", "string2"]
    //  row 1 [NULL, "string1", NULL, "string2", "string3"]
    //  row 2 ["string3", "string4", "string1"]

    // Filer is "flowing" nodes (i.e., all nodes in the filter need to be present in the activity array):
    // ["string1", "string3"].
    // The second and the third row pass the filter (NULL values are ignored).
    array->append_datum(DatumArray{"string1", "string2"});
    array->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    array->append_datum(DatumArray{"string3", "string4", "string1"});

    auto nodes_filter = create_const_filter(DatumArray{"string1", "string3"}, 3);
    auto empty_filter = create_const_filter(DatumArray{}, 3);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(),
            {array, empty_filter, nodes_filter, empty_filter, empty_filter, empty_filter, empty_filter}).value();
    EXPECT_EQ(3, result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_string_data_nullable) {
    // Similar to above but the input array is declared as NULLable.
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    array->append_datum(DatumArray{"string1", "string2"});
    array->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    array->append_datum(DatumArray{"string3", "string4", "string1"});
    array->append_nulls(1);

    auto nodes_filter = create_const_filter(DatumArray{"string1", "string3"}, 4);
    auto empty_filter = create_const_filter(DatumArray{}, 4);

    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(),
            {array, empty_filter, nodes_filter, empty_filter, empty_filter, empty_filter, empty_filter}).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
    EXPECT_FALSE(result->get(0).is_null());
    EXPECT_FALSE(result->get(1).is_null());
    EXPECT_FALSE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_excluding_nodes) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    array->append_datum(DatumArray{"string1", "string2"});
    array->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    array->append_datum(DatumArray{"string3", "string4", "string1"});
    array->append_datum(DatumArray{"string1", "string3", "string2"});
    array->append_nulls(1);

    auto nodes_filter = create_const_filter(DatumArray{"string1", "string3"}, 5);
    auto excluding_nodes_filter = create_const_filter(DatumArray{"string2"}, 5);
    auto empty_filter = create_const_filter(DatumArray{}, 5);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(), {array, empty_filter, nodes_filter, empty_filter, excluding_nodes_filter, empty_filter,
                        empty_filter}).value();
    EXPECT_EQ(5, result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(0, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
    EXPECT_EQ(0, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_only_excluding_nodes) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{"string1", "string2"});
    array->append_datum(DatumArray{"string3", "string4", "string1"});
    array->append_datum(DatumArray{kNullDatum});
    array->append_datum(DatumArray{"string3"});

    auto excluding_nodes_filter = create_const_filter(DatumArray{"string2"}, 4);
    auto empty_filter = create_const_filter(DatumArray{}, 4);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(), {array, empty_filter, empty_filter, empty_filter, excluding_nodes_filter, empty_filter,
                        empty_filter}).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(0, result->get(2).get_int64());
    EXPECT_EQ(1, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_start_and_end_nodes) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{"start1", "end2"});
    array->append_datum(DatumArray{"start2", "end1"});
    array->append_datum(DatumArray{"start2", "excluding_activity", "end1"});
    array->append_datum(DatumArray{"start2", "end1", "string1"});
    array->append_datum(DatumArray{"string1", "start2", "end1"});
    array->append_datum(DatumArray{kNullDatum, "start1", "end2", kNullDatum});
    array->append_datum(DatumArray{"start2", "end1", kNullDatum});

    auto start_nodes_filter = create_const_filter(DatumArray{"start1", "start2"}, 7);
    auto end_nodes_filter = create_const_filter(DatumArray{"end1", "end2"}, 7);
    auto excluding_nodes_filter = create_const_filter(DatumArray{"excluding_activity"}, 7);
    auto empty_filter = create_const_filter(DatumArray{}, 7);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(),
            {array, start_nodes_filter, empty_filter, end_nodes_filter, excluding_nodes_filter, empty_filter,
             empty_filter}).value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_excluding_all_nodes) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{"B", "A"});
    array->append_datum(DatumArray{"B", "D"});
    array->append_datum(DatumArray{"B", "C", "E"});
    array->append_datum(DatumArray{"A", "C"});
    array->append_datum(DatumArray{kNullDatum});
    array->append_datum(DatumArray{"C", kNullDatum, "A"});
    array->append_datum(DatumArray{kNullDatum, "A"});
    array->append_datum(DatumArray{});
    array->append_datum(DatumArray{"D"});

    auto excluding_all_nodes_filter = create_const_filter(DatumArray{"A", "C"}, 9);
    auto empty_filter = create_const_filter(DatumArray{}, 9);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(),
            {array, empty_filter, empty_filter, empty_filter, empty_filter, excluding_all_nodes_filter,
             empty_filter}).value();
    EXPECT_EQ(9, result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
    EXPECT_EQ(1L, result->get(8).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_nodes_any) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{"B", "A"});
    array->append_datum(DatumArray{"B", "D"});
    array->append_datum(DatumArray{"B", "C", "E"});
    array->append_datum(DatumArray{"A", "C"});
    array->append_datum(DatumArray{kNullDatum});
    array->append_datum(DatumArray{"C", kNullDatum});
    array->append_datum(DatumArray{kNullDatum, "A"});
    array->append_datum(DatumArray{});

    auto nodes_any_filter = create_const_filter(DatumArray{"A", "C"}, 8);
    auto empty_filter = create_const_filter(DatumArray{}, 8);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(),
            {array, empty_filter, empty_filter, empty_filter, empty_filter, empty_filter,
             nodes_any_filter}).value();
    EXPECT_EQ(8, result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_empty_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto start_nodes_filter = create_const_filter(DatumArray{"start1", "start2"}, 0);
    auto end_nodes_filter = create_const_filter(DatumArray{"end1", "end2"}, 0);
    auto excluding_nodes_filter = create_const_filter(DatumArray{"excluding_activity"}, 0);
    auto empty_filter = create_const_filter(DatumArray{}, 0);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(),
            {array, start_nodes_filter, empty_filter, end_nodes_filter, excluding_nodes_filter, empty_filter,
             empty_filter}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_non_const_filters) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{"start1", "end2"});
    array->append_datum(DatumArray{"start2", "end1"});
    array->append_datum(DatumArray{kNullDatum, "start1", "end2", kNullDatum});
    array->append_datum(DatumArray{"start2", "end1", kNullDatum});

    auto start_nodes_filter = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    start_nodes_filter->append_datum(DatumArray{"start1"});
    start_nodes_filter->append_datum(DatumArray{"start2"});
    start_nodes_filter->append_datum(DatumArray{});
    start_nodes_filter->append_datum(DatumArray{"start2"});
    auto end_nodes_filter = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    end_nodes_filter->append_datum(DatumArray{});
    end_nodes_filter->append_datum(DatumArray{"end1"});
    end_nodes_filter->append_datum(DatumArray{});
    end_nodes_filter->append_datum(DatumArray{"end2"});
    auto empty_filter = create_const_filter(DatumArray{}, 7);
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(),
            {array, start_nodes_filter, empty_filter, end_nodes_filter, empty_filter, empty_filter,
             empty_filter}).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

} // namespace starrocks
