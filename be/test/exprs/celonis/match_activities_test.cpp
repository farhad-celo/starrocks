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

    auto nodes_filter = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    nodes_filter->append_datum(DatumArray{"string1", "string3"});
    auto other_filters = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    other_filters->append_datum(DatumArray{});
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(), {array, other_filters, nodes_filter, other_filters, other_filters, other_filters, other_filters}).value();
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

    auto nodes_filter = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    nodes_filter->append_datum(DatumArray{"string1", "string3"});
    auto other_filters = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    other_filters->append_datum(DatumArray{});
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(), {array, other_filters, nodes_filter, other_filters, other_filters, other_filters, other_filters}).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
    EXPECT_FALSE(result->get(0).is_null());
    EXPECT_FALSE(result->get(1).is_null());
    EXPECT_FALSE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
}

#if !defined(__SANITIZE_ADDRESS__)
TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_unsupported_filter) {
    // Similar to above, but provides node information in one of the filters that is not supported for now ("starting"
    // nodes).
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    // Input data:
    //  row 0 ["string1", "string2"]
    //  row 1 [NULL, "string1", NULL, "string2", "string3"]
    array->append_datum(DatumArray{"string1", "string2"});
    array->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    auto nodes_filter = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    nodes_filter->append_datum(DatumArray{"string1", "string2"});
    auto other_filters = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    other_filters->append_datum(DatumArray{});
    EXPECT_THROW(CelonisMatchActivitiesFunctions::celonis_match_activities(ctx.get(),
                                                                           {array, nodes_filter, nodes_filter,
                                                                            other_filters, other_filters,
                                                                            other_filters, other_filters}),
                 std::runtime_error);
}
#endif

} // namespace starrocks
