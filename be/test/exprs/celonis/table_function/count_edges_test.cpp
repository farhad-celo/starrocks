#include "exprs/celonis/table_function/count_edges.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "../util.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "testutil/assert.h"

namespace starrocks {

class CelonisCountEdgesTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
};

TEST_F(CelonisCountEdgesTest, count_edges) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{"a", "b", "b", "b", "c", "c", "c", "b", "c", "c"});
    array->append_datum(DatumArray{"a", "b"});
    array->append_datum(DatumArray{});
    array->append_datum(DatumArray{"b"});

    TableFunctionState* table_state;
    auto function = std::make_unique<CountEdges>();
    Columns input;
    input.push_back(array);
    ASSERT_OK(function->init({}, &table_state));
    table_state->set_params(input);
    ASSERT_OK(function->prepare(table_state));

    auto [result, offset] = function->process(nullptr, table_state);

    EXPECT_EQ(4, table_state->processed_rows());

    // result[0] is source, result[1] is target, result[2] is count
    ASSERT_EQ(6, result[0]->size());
    ASSERT_EQ(6, result[1]->size());
    ASSERT_EQ(6, result[2]->size());

    EXPECT_EQ("a", result[0]->get(0).get_slice());
    EXPECT_EQ("b", result[1]->get(0).get_slice());
    EXPECT_EQ(1, result[2]->get(0).get_int64());

    EXPECT_EQ("b", result[0]->get(1).get_slice());
    EXPECT_EQ("b", result[1]->get(1).get_slice());
    EXPECT_EQ(2, result[2]->get(1).get_int64());

    EXPECT_EQ("b", result[0]->get(2).get_slice());
    EXPECT_EQ("c", result[1]->get(2).get_slice());
    EXPECT_EQ(2, result[2]->get(2).get_int64());

    EXPECT_EQ("c", result[0]->get(3).get_slice());
    EXPECT_EQ("c", result[1]->get(3).get_slice());
    EXPECT_EQ(3, result[2]->get(3).get_int64());

    EXPECT_EQ("c", result[0]->get(4).get_slice());
    EXPECT_EQ("b", result[1]->get(4).get_slice());
    EXPECT_EQ(1, result[2]->get(4).get_int64());

    EXPECT_EQ("a", result[0]->get(5).get_slice());
    EXPECT_EQ("b", result[1]->get(5).get_slice());
    EXPECT_EQ(1, result[2]->get(5).get_int64());

    ASSERT_EQ(5, offset->size());
    EXPECT_EQ(0, offset->get(0).get_int32());
    EXPECT_EQ(5, offset->get(1).get_int32());
    EXPECT_EQ(6, offset->get(2).get_int32());
    EXPECT_EQ(6, offset->get(2).get_int32());
    EXPECT_EQ(6, offset->get(2).get_int32());
    function->close(nullptr, table_state);
}

TEST_F(CelonisCountEdgesTest, count_edges_null) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    array->append_datum(DatumArray{"a", "b", "b", "b", Datum(), "b", Datum(), "c"});
    array->append_datum(Datum());
    array->append_datum(DatumArray{});
    TableFunctionState* table_state;
    auto function = std::make_unique<CountEdges>();
    Columns input;
    input.push_back(array);
    ASSERT_OK(function->init({}, &table_state));
    table_state->set_params(input);
    ASSERT_OK(function->prepare(table_state));

    auto [result, offset] = function->process(nullptr, table_state);

    EXPECT_EQ(3, table_state->processed_rows());

    // result[0] is source, result[1] is target, result[2] is count
    ASSERT_EQ(3, result[0]->size());
    ASSERT_EQ(3, result[1]->size());
    ASSERT_EQ(3, result[2]->size());

    EXPECT_EQ("a", result[0]->get(0).get_slice());
    EXPECT_EQ("b", result[1]->get(0).get_slice());
    EXPECT_EQ(1, result[2]->get(0).get_int64());

    EXPECT_EQ("b", result[0]->get(1).get_slice());
    EXPECT_EQ("b", result[1]->get(1).get_slice());
    EXPECT_EQ(3, result[2]->get(1).get_int64());

    EXPECT_EQ("b", result[0]->get(2).get_slice());
    EXPECT_EQ("c", result[1]->get(2).get_slice());
    EXPECT_EQ(1, result[2]->get(2).get_int64());

    ASSERT_EQ(4, offset->size());
    EXPECT_EQ(0, offset->get(0).get_int32());
    EXPECT_EQ(3, offset->get(1).get_int32());
    EXPECT_EQ(3, offset->get(2).get_int32());
    EXPECT_EQ(3, offset->get(3).get_int32());
    function->close(nullptr, table_state);
}

} // namespace starrocks
