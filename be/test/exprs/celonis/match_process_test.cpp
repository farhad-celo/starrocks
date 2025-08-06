#include "exprs/celonis/match_process.h"

#include "util.h"
#include <gtest/gtest.h>
#include "column/column_helper.h"
#include "column/vectorized_fwd.h"
#include "exprs/function_context.h"

namespace starrocks::vectorized {

class CelonisMatchProcessTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

    void match_process(Columns columns, const std::vector<int>& res) {
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
        auto context = ctx.get();
        context->set_constant_columns(columns);

        ASSERT_TRUE(
                CelonisMatchProcess::match_process_prepare(context,
                                                           FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

        const auto result = CelonisMatchProcess::celonis_match_process(context, columns).value();
        const auto v = ColumnHelper::as_column<Int64Column>(result);

        for (int i = 0; i < res.size(); ++i) {
            EXPECT_EQ(res[i], v->get(i).get_int64());
        }
        ASSERT_TRUE(
                CelonisMatchProcess::match_process_close(context,
                                                         FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                        .ok());
    }
};

TEST_F(CelonisMatchProcessTest, celonis_match_b_a) {
    Slice jsonInput("{\n"
                    "  \"states\" : [ {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 0 ],\n"
                    "      \"activityNames\" : [\"B\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"E_TRANSITION\",\n"
                    "      \"toStates\" : [ 0 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    }, {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 1, 0 ],\n"
                    "      \"activityNames\" : [\"A\"]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"B\"]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"B\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"E_TRANSITION\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    }, {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"A\"]\n"
                    "    } ],\n"
                    "    \"final\" : true\n"
                    "  } ],\n"
                    "  \"initialState\" : 0\n"
                    "}");
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B"});
    array->append_datum(DatumArray{"D"});

    auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    json_spec->append_datum(Datum{jsonInput});

    Columns input;
    input.push_back(array);
    input.push_back(json_spec);

    match_process(input, {1, 0});
}

TEST_F(CelonisMatchProcessTest, celonis_match_b_a_old_unmatched) {
    Slice jsonInput("{\n"
                    "  \"states\" : [ {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 0 ],\n"
                    "      \"activityNames\" : [\"B\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"UNMATCHED\",\n"
                    "      \"toStates\" : [ 0 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    }, {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 1, 0 ],\n"
                    "      \"activityNames\" : [\"A\"]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"B\"]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"B\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"UNMATCHED\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    }, {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"A\"]\n"
                    "    } ],\n"
                    "    \"final\" : true\n"
                    "  } ],\n"
                    "  \"initialState\" : 0\n"
                    "}");
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B"});
    array->append_datum(DatumArray{kNullDatum, "A", kNullDatum, kNullDatum, "B", kNullDatum});
    array->append_datum(DatumArray{"D"});
    array->append_datum(DatumArray{"D", kNullDatum});

    auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    json_spec->append_datum(Datum{jsonInput});

    Columns input;
    input.push_back(array);
    input.push_back(json_spec);

    match_process(input, {1, 1, 0, 0});
}

TEST_F(CelonisMatchProcessTest, celonis_match_a_b_eventually) {
    Slice jsonInput("{\n"
                    "  \"states\" : [ {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 0, 1 ],\n"
                    "      \"activityNames\" : [\"A\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"UNMATCHED2\",\n"
                    "      \"toStates\" : [ 0 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 1, 2 ],\n"
                    "      \"activityNames\" : [\"B\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"UNMATCHED2\",\n"
                    "      \"toStates\" : [ 1 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"UNMATCHED2\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    } ],\n"
                    "    \"final\" : true\n"
                    "  } ],\n"
                    "  \"initialState\" : 0\n"
                    "}");
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B"});
    array->append_datum(DatumArray{"D"});
    array->append_datum(DatumArray{"A", "1", "B", "2"});
    array->append_datum(DatumArray{"1", "A", "2", "3", "4", "B", "B"});
    array->append_datum(DatumArray{"A", "A"});

    auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    json_spec->append_datum(Datum{jsonInput});

    Columns input;
    input.push_back(array);
    input.push_back(json_spec);

    match_process(input, {1, 0, 1, 1, 0});
}

TEST_F(CelonisMatchProcessTest, celonis_match_b_a_like) {
    // Same as above but using LIKE
    Slice jsonInput("{\n"
                    "  \"states\" : [ {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"LIKE\",\n"
                    "      \"toStates\" : [ 0 ],\n"
                    "      \"activityNames\" : [\"B%\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"E_TRANSITION\",\n"
                    "      \"toStates\" : [ 0 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    }, {\n"
                    "      \"type\" : \"LIKE\",\n"
                    "      \"toStates\" : [ 1, 0 ],\n"
                    "      \"activityNames\" : [\"ABC%\"]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"LIKE\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"BCD%\"]\n"
                    "    } ],\n"
                    "    \"final\" : false\n"
                    "  }, {\n"
                    "    \"transitions\" : [ {\n"
                    "      \"type\" : \"LIKE\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"BCD%\"]\n"
                    "    }, {\n"
                    "      \"type\" : \"E_TRANSITION\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [ ]\n"
                    "    }, {\n"
                    "      \"type\" : \"EXACT_MATCH\",\n"
                    "      \"toStates\" : [ 2 ],\n"
                    "      \"activityNames\" : [\"A\"]\n"
                    "    } ],\n"
                    "    \"final\" : true\n"
                    "  } ],\n"
                    "  \"initialState\" : 0\n"
                    "}");
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B"});
    array->append_datum(DatumArray{"ABCD", "BCDE"});

    auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    json_spec->append_datum(Datum{jsonInput});

    Columns input;
    input.push_back(array);
    input.push_back(json_spec);

    match_process(input, {0, 1});
}


TEST_F(CelonisMatchProcessTest, celonis_match_activities_inverse_match) {
    Slice jsonInput("{\n"
                     "  \"states\" : [ {\n"
                     "    \"transitions\" : [ {\n"
                     "      \"type\" : \"INVERSE_MATCH\",\n"
                     "      \"toStates\" : [ 1 ],\n"
                     "      \"activityNames\" : [ \"A\", \"B\" ]\n"
                     "    } ],\n"
                     "    \"final\" : false\n"
                     "  }, {\n"
                     "    \"transitions\" : [ {\n"
                     "      \"type\" : \"E_TRANSITION\",\n"
                     "      \"toStates\" : [ 2 ],\n"
                     "      \"activityNames\" : [ ]\n"
                     "    } ],\n"
                     "    \"final\" : false\n"
                     "  }, {\n"
                     "    \"transitions\" : [ {\n"
                     "      \"type\" : \"INVERSE_MATCH\",\n"
                     "      \"toStates\" : [ 3 ],\n"
                     "      \"activityNames\" : [ \"A\", \"B\" ]\n"
                     "    } ],\n"
                     "    \"final\" : true\n"
                     "  }, {\n"
                     "    \"transitions\" : [ {\n"
                     "      \"type\" : \"E_TRANSITION\",\n"
                     "      \"toStates\" : [ 2 ],\n"
                     "      \"activityNames\" : [ ]\n"
                     "    } ],\n"
                     "    \"final\" : true\n"
                     "  } ],\n"
                     "  \"initialState\" : 0\n"
                     "}");
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B"});
    array->append_datum(DatumArray{"C", "D"});

    auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    json_spec->append_datum(Datum{jsonInput});
    Columns input;
    input.push_back(array);
    input.push_back(json_spec);

    match_process(input, {0, 1});
}

TEST_F(CelonisMatchProcessTest, const_null_column) {
    auto array = ColumnHelper::create_const_null_column(2);
    auto json_spec = ColumnHelper::create_const_null_column(2);
    Columns columns;
    columns.push_back(array);
    columns.push_back(json_spec);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto context = ctx.get();
    context->set_constant_columns(columns);

    ASSERT_TRUE(
            CelonisMatchProcess::match_process_prepare(context,
                                                       FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

    Columns columns_for_call;
    columns_for_call.push_back(array);
    columns_for_call.push_back(json_spec);
    const auto result = CelonisMatchProcess::celonis_match_process(context, columns_for_call).value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->only_null());
    EXPECT_TRUE(result->is_constant());
}

} // namespace starrocks::vectorized
