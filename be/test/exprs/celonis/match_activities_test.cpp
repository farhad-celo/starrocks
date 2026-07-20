#include "exprs/celonis/match_activities.h"

#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "types/logical_type.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {
namespace {
TypeDescriptor array_type(const LogicalType &child_type) {
    TypeDescriptor t;
    t.type = TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = child_type;
    // specify length for VARCHAR(10) type.
    t.children[0].len = child_type == TYPE_VARCHAR ? 10 : child_type == TYPE_CHAR ? 10 : -1;
    return t;
}
} // namespace

class CelonisMatchActivitiesTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = array_type(TYPE_VARCHAR);
};

TEST_F(CelonisMatchActivitiesTest, array_celonis_source_string_data) {
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
    EXPECT_EQ(0, result->get(0).get_uint8());
    EXPECT_EQ(1, result->get(1).get_uint8());
    EXPECT_EQ(1, result->get(2).get_uint8());
}

TEST_F(CelonisMatchActivitiesTest, array_celonis_source_string_data_nullable) {
    // Similar to above but the input array is declared as NULLable.
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    array->append_datum(DatumArray{"string1", "string2"});
    array->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    array->append_datum(DatumArray{"string3", "string4", "string1"});

    auto nodes_filter = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    nodes_filter->append_datum(DatumArray{"string1", "string3"});
    auto other_filters = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    other_filters->append_datum(DatumArray{});
    const auto result = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(), {array, other_filters, nodes_filter, other_filters, other_filters, other_filters, other_filters}).value();
    EXPECT_EQ(3, result->size());
    EXPECT_EQ(0, result->get(0).get_uint8());
    EXPECT_EQ(1, result->get(1).get_uint8());
    EXPECT_EQ(1, result->get(2).get_uint8());
}


TEST_F(CelonisMatchActivitiesTest, array_celonis_source_unsupported_filter) {
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
    std::ignore = CelonisMatchActivitiesFunctions::celonis_match_activities(
            ctx.get(), {array, nodes_filter, nodes_filter, other_filters, other_filters, other_filters, other_filters});
    ASSERT_TRUE(ctx->has_error());
}
} // namespace starrocks
