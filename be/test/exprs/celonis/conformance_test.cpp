#include "exprs/celonis/conformance.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <testutil/assert.h>

#include "column/column_helper.h"
#include "column/vectorized_fwd.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks::vectorized {

const int64_t id_A = 958484639;
const int64_t id_B = 601389851;
const int64_t id_C = 104740979;

class CelonisConformanceTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);

    void evaluate(Column* result, Column* expected) {
        ASSERT_EQ(result->size(), expected->size());
        for (int i = 0; i < result->size(); ++i) {
            if (result->get(i).is_null() || expected->get(i).is_null()) {
                EXPECT_EQ(result->get(i).is_null(), expected->get(i).is_null());
                continue;
            }
            auto result_array = result->get(i).get_array();
            auto expected_array = expected->get(i).get_array();
            ASSERT_EQ(result_array.size(), expected_array.size());
            for (int j = 0; j < result_array.size(); j++) {
                EXPECT_EQ(result_array[j].get_int64(), expected_array[j].get_int64())
                                    << "row index: " << i << ", element index: " << j;
            }
        }
    }

    Status conform(const Columns& columns, Column* expected) {
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
        auto context = ctx.get();
        context->set_constant_columns(columns);

        DeferOp close_fragment_local([&context] {
            CelonisConformance::conformance_close(context,
                                                  FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisConformance::conformance_prepare(context, FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        DeferOp close_thread_local([&context] {
            CelonisConformance::conformance_close(context,
                                                  FunctionContext::FunctionContext::FunctionStateScope::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisConformance::conformance_prepare(context, FunctionContext::FunctionStateScope::THREAD_LOCAL));

        const auto result = CelonisConformance::conformance(context, columns).value();
        evaluate(result.get(), expected);

        return Status::OK();
    }
};

TEST_F(CelonisConformanceTest, pql_conformance_examples) {
    Slice jsonInput(
            R"json({
              "places" : [ "P_0", "P_1", "P_2" ],
              "transitions" : [ "T_01", "T_12" ],
              "arcs" : [
                {
                  "from" : "P_0",
                  "to" : "T_01"
                }, {
                  "from" : "T_01",
                  "to" : "P_1"
                }, {
                  "from" : "P_1",
                  "to" : "T_12"
                }, {
                  "from" : "T_12",
                  "to" : "P_2"
                }
              ],
              "mapping" : [
                {
                  "from" : "A",
                  "to" : "T_01"
                }, {
                  "from" : "B",
                  "to" : "T_12"
                }
              ],
              "initial_marking" : [
                {
                  "node" : "P_0",
                  "count" : 1
                }
              ],
              "final_marking" : [
                {
                  "node" : "P_2",
                  "count" : 1
                }
              ]
            })json");
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto expected = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, false);

    array->append_datum(DatumArray{"A", "B"});
    expected->append_datum(DatumArray{0L, 0L});

    array->append_datum(DatumArray{"A", "C"});
    expected->append_datum(DatumArray{0L, /*C is an undesired activity*/ -id_C});

    array->append_datum(DatumArray{"A"});
    expected->append_datum(DatumArray{2147483647L});

    array->append_datum(DatumArray{"A", "A"});
    expected->append_datum(DatumArray{0L, /*A is followed by A*/ (id_A << 32) + id_A});

    array->append_datum(DatumArray{"B", "A", "B"});
    expected->append_datum(
            DatumArray{/*B executed as start activity */ (MISSING_START_ACTIVITY_KEY << 32) + id_B, 0L, 0L});

    array->append_datum(DatumArray{"A", "C", "A"});
    expected->append_datum(
            DatumArray{0L, /*C is an undesired activity*/ -id_C, /*A is followed by A*/(id_A << 32) + id_A});

    // NULL handling. A NULL value conforms with any Petri net.
    array->append_datum(DatumArray{"A", Datum{}, "B"});
    expected->append_datum(DatumArray{0L, 0L, 0L});

    auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, array->size());

    Columns input;
    input.push_back(array);
    input.push_back(json_spec);

    EXPECT_OK(conform(input, expected.get()));
}

TEST_F(CelonisConformanceTest, invalid_json_spec) {
    Slice jsonInput(
            R"json({
              "places" : [ "P_0", "P_1", "P_2" ],
              "transitions" : [ "T_01", "T_12" ]
            })json");

    auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, 1);

    Columns input;
    input.push_back(nullptr);
    input.push_back(json_spec);

    EXPECT_THAT(std::string(conform(input, nullptr).message()), testing::HasSubstr("does not contain 'mapping'."));
}

TEST_F(CelonisConformanceTest, pql_conformance_input_handling) {
    Slice jsonInput(
            R"json({
              "places" : [ "P_0", "P_1", "P_2" ],
              "transitions" : [ "T_01", "T_12" ],
              "arcs" : [
                {
                  "from" : "P_0",
                  "to" : "T_01"
                }, {
                  "from" : "T_01",
                  "to" : "P_1"
                }, {
                  "from" : "P_1",
                  "to" : "T_12"
                }, {
                  "from" : "T_12",
                  "to" : "P_2"
                }
              ],
              "mapping" : [
                {
                  "from" : "A",
                  "to" : "T_01"
                }, {
                  "from" : "B",
                  "to" : "T_12"
                }
              ],
              "initial_marking" : [
                {
                  "node" : "P_0",
                  "count" : 1
                }
              ],
              "final_marking" : [
                {
                  "node" : "P_2",
                  "count" : 1
                }
              ]
            })json");

    {
        // NULL literal
        int size = 4;
        auto array = ColumnHelper::create_const_null_column(size);
        auto expected = ColumnHelper::create_const_null_column(size);

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, size);

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        EXPECT_OK(conform(input, expected.get()));
    }
    {
        // Const array
        int size = 4;
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        array->append_datum(DatumArray{"A", "C"});
        array = ConstColumn::create(array, size);

        auto expected = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
        for (int i = 0; i < size; ++i) {
            expected->append_datum(DatumArray{0L, /*C is an undesired activity*/ -id_C});
        }

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, size);

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        EXPECT_OK(conform(input, expected.get()));
    }
}

class CelonisReadableConformanceTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

    void evaluate(Column* result, Column* expected) {
        ASSERT_EQ(result->size(), expected->size());
        for (int i = 0; i < result->size(); ++i) {
            if (result->get(i).is_null() || expected->get(i).is_null()) {
                EXPECT_EQ(result->get(i).is_null(), expected->get(i).is_null());
                continue;
            }
            auto result_array = result->get(i).get_array();
            auto expected_array = expected->get(i).get_array();
            ASSERT_EQ(result_array.size(), expected_array.size());
            for (int j = 0; j < result_array.size(); j++) {
                EXPECT_EQ(result_array[j].get_slice(), expected_array[j].get_slice())
                                    << "row index: " << i << ", element index: " << j;
            }
        }
    }

    Status conform(const Columns& columns, Column* expected) {
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
        auto context = ctx.get();
        context->set_constant_columns(columns);

        DeferOp close_fragment_local([&context] {
            CelonisConformance::conformance_close(context,
                                                  FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisConformance::conformance_prepare(context, FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        DeferOp close_thread_local([&context] {
            CelonisConformance::conformance_close(context,
                                                  FunctionContext::FunctionContext::FunctionStateScope::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisConformance::conformance_prepare(context, FunctionContext::FunctionStateScope::THREAD_LOCAL));

        const auto result = CelonisConformance::readable_conformance(context, columns).value();
        evaluate(result.get(), expected);

        return Status::OK();
    }
};

TEST_F(CelonisReadableConformanceTest, pql_conformance_examples) {
    Slice jsonInput(
            R"json({
              "places" : [ "P_0", "P_1", "P_2" ],
              "transitions" : [ "T_01", "T_12" ],
              "arcs" : [
                {
                  "from" : "P_0",
                  "to" : "T_01"
                }, {
                  "from" : "T_01",
                  "to" : "P_1"
                }, {
                  "from" : "P_1",
                  "to" : "T_12"
                }, {
                  "from" : "T_12",
                  "to" : "P_2"
                }
              ],
              "mapping" : [
                {
                  "from" : "A",
                  "to" : "T_01"
                }, {
                  "from" : "B",
                  "to" : "T_12"
                }
              ],
              "initial_marking" : [
                {
                  "node" : "P_0",
                  "count" : 1
                }
              ],
              "final_marking" : [
                {
                  "node" : "P_2",
                  "count" : 1
                }
              ]
            })json");
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    auto expected = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B"});
    expected->append_datum(DatumArray{"Conforms", "Conforms"});

    array->append_datum(DatumArray{"A", "C"});
    expected->append_datum(DatumArray{"Conforms", "C is an undesired activity"});

    array->append_datum(DatumArray{"A"});
    expected->append_datum(DatumArray{"Incomplete"});

    array->append_datum(DatumArray{"A", "A"});
    expected->append_datum(DatumArray{"Conforms", "A is followed by A"});

    array->append_datum(DatumArray{"B", "A", "B"});
    expected->append_datum(DatumArray{"B is executed as start activity", "Conforms", "Conforms"});

    array->append_datum(DatumArray{"A", "C", "A"});
    expected->append_datum(DatumArray{"Conforms", "C is an undesired activity", "A is followed by A"});

    // NULL handling. A NULL value conforms with any Petri net.
    array->append_datum(DatumArray{"A", Datum{}, "B"});
    expected->append_datum(DatumArray{"Conforms", "Conforms", "Conforms"});

    array->append_datum(DatumArray{"B", "C", "A", "C", Datum{}, "A", "B"});
    expected->append_datum(DatumArray{"B is executed as start activity", "C is an undesired activity", "Conforms",
                                      "C is an undesired activity", "Conforms", "A is followed by A", "Conforms"});

    auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, array->size());

    Columns input;
    input.push_back(array);
    input.push_back(json_spec);

    EXPECT_OK(conform(input, expected.get()));
}

TEST_F(CelonisReadableConformanceTest, pql_conformance_input_handling) {
    Slice jsonInput(
            R"json({
              "places" : [ "P_0", "P_1", "P_2" ],
              "transitions" : [ "T_01", "T_12" ],
              "arcs" : [
                {
                  "from" : "P_0",
                  "to" : "T_01"
                }, {
                  "from" : "T_01",
                  "to" : "P_1"
                }, {
                  "from" : "P_1",
                  "to" : "T_12"
                }, {
                  "from" : "T_12",
                  "to" : "P_2"
                }
              ],
              "mapping" : [
                {
                  "from" : "A",
                  "to" : "T_01"
                }, {
                  "from" : "B",
                  "to" : "T_12"
                }
              ],
              "initial_marking" : [
                {
                  "node" : "P_0",
                  "count" : 1
                }
              ],
              "final_marking" : [
                {
                  "node" : "P_2",
                  "count" : 1
                }
              ]
            })json");

    {
        // NULL literal
        int size = 4;
        auto array = ColumnHelper::create_const_null_column(size);
        auto expected = ColumnHelper::create_const_null_column(size);

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, size);

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        EXPECT_OK(conform(input, expected.get()));
    }
    {
        // Const array
        int size = 4;
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        array->append_datum(DatumArray{"A", "C"});
        array = ConstColumn::create(array, size);

        auto expected = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        for (int i = 0; i < size; ++i) {
            expected->append_datum(DatumArray{"Conforms", "C is an undesired activity"});
        }

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, size);

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        EXPECT_OK(conform(input, expected.get()));
    }
}
} // namespace starrocks::vectorized

