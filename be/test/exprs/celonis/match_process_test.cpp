#include "exprs/celonis/match_process.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/vectorized_fwd.h"
#include "exprs/function_context.h"
#include "util.h"

namespace starrocks::vectorized {

class CelonisMatchProcessTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

    void assert_invalid_spec(const std::string& json, const std::string& expected_error) {
        SCOPED_TRACE(json);
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        array->append_datum(DatumArray{"a", "b"});
        auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
        json_spec->append_datum(Datum{Slice(json)});
        std::unique_ptr<FunctionContext> context(FunctionContext::create_test_context());
        context->set_constant_columns({array, json_spec});

        const auto status = CelonisMatchProcess::match_process_prepare(context.get(), FunctionContext::FRAGMENT_LOCAL);
        EXPECT_FALSE(status.ok());
        EXPECT_NE(std::string::npos, status.to_string().find("celonis_match_process"));
        EXPECT_NE(std::string::npos, status.to_string().find(expected_error));
        EXPECT_EQ(nullptr, context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        EXPECT_TRUE(CelonisMatchProcess::match_process_close(context.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }

    void match_process(Columns columns, const std::vector<int>& res) {
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
        auto context = ctx.get();
        context->set_constant_columns(columns);

        ASSERT_TRUE(
                CelonisMatchProcess::match_process_prepare(context, FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                        .ok());

        const auto result = CelonisMatchProcess::celonis_match_process(context, columns).value();
        const auto v = ColumnHelper::as_column<Int64Column>(result);

        for (int i = 0; i < res.size(); ++i) {
            EXPECT_EQ(res[i], v->get(i).get_int64());
        }
        ASSERT_TRUE(CelonisMatchProcess::match_process_close(
                            context, FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                            .ok());
    }
};

TEST_F(CelonisMatchProcessTest, invalid_json) {
    for (const auto* json : {"", "{", "null", "[]", "true", "1", "\"spec\""}) {
        assert_invalid_spec(json, "valid JSON object");
    }
}

TEST_F(CelonisMatchProcessTest, invalid_initial_state) {
    assert_invalid_spec(R"({"states":[{"final":false,"transitions":[]}]})", "initialState");
    for (const auto* index : {"-1", "1", "999999", "2147483647", "2147483648", "-2147483649", "4294967296", "0.5",
                              "null", "true", "\"0\"", "[]", "{}"}) {
        assert_invalid_spec(
                std::string(R"({"initialState":)") + index + R"(,"states":[{"final":false,"transitions":[]}]})",
                "initialState");
    }
}

TEST_F(CelonisMatchProcessTest, invalid_states) {
    for (const auto* json : {R"({})", R"({"initialState":0})", R"({"initialState":0,"states":null})",
                             R"({"initialState":0,"states":{}})", R"({"initialState":0,"states":[]})"}) {
        assert_invalid_spec(json, "non-empty array");
    }
    for (const auto* state :
         {"null", "[]", "false", "{}", R"({"transitions":[]})", R"({"final":0,"transitions":[]})", R"({"final":false})",
          R"({"final":false,"transitions":null})", R"({"final":false,"transitions":{}})"}) {
        assert_invalid_spec(std::string(R"({"initialState":0,"states":[)") + state + "]}", "NFA states");
    }
}

TEST_F(CelonisMatchProcessTest, invalid_transitions) {
    for (const auto* transition :
         {"null", "[]", "false", "{}", R"({"toStates":[0],"activityNames":[]})",
          R"({"type":0,"toStates":[0],"activityNames":[]})", R"({"type":"EXACT_MATCH","activityNames":[]})",
          R"({"type":"EXACT_MATCH","toStates":null,"activityNames":[]})",
          R"({"type":"EXACT_MATCH","toStates":{},"activityNames":[]})", R"({"type":"EXACT_MATCH","toStates":[0]})",
          R"({"type":"EXACT_MATCH","toStates":[0],"activityNames":null})",
          R"({"type":"EXACT_MATCH","toStates":[0],"activityNames":{}})"}) {
        assert_invalid_spec(
                std::string(R"({"initialState":0,"states":[{"final":false,"transitions":[)") + transition + "]}]}",
                "NFA transitions");
    }
    assert_invalid_spec(R"({"initialState":0,"states":[{"final":false,"transitions":[
                        {"type":"UNKNOWN","toStates":[0],"activityNames":[]}]}]})",
                        "Unsupported TransitionType");
}

TEST_F(CelonisMatchProcessTest, invalid_transition_targets) {
    for (const auto* type : {"E_TRANSITION", "UNMATCHED", "UNMATCHED2", "EXACT_MATCH", "INVERSE_MATCH", "LIKE"}) {
        for (const auto* index : {"-1", "1", "999999", "2147483647", "2147483648", "-2147483649", "4294967296", "0.5",
                                  "null", "true", "\"0\"", "[]", "{}"}) {
            assert_invalid_spec(std::string(R"({"initialState":0,"states":[{"final":false,"transitions":[{"type":")") +
                                        type + R"(","toStates":[0, )" + index + R"(],"activityNames":["%"]}]}]})",
                                "toStates");
        }
    }
    // Validate targets even in states that cannot be reached from the initial state.
    assert_invalid_spec(R"({"initialState":0,"states":[{"final":true,"transitions":[]},
                        {"final":false,"transitions":[{"type":"EXACT_MATCH","toStates":[2],"activityNames":["a"]}]}]})",
                        "toStates");
}

TEST_F(CelonisMatchProcessTest, transition_type_with_embedded_nul) {
    for (const auto* type : {"E_TRANSITION", "UNMATCHED", "UNMATCHED2", "EXACT_MATCH", "INVERSE_MATCH", "LIKE"}) {
        assert_invalid_spec(std::string(R"({"initialState":0,"states":[{"final":false,"transitions":[{"type":")") +
                                    type + R"(\u0000UNKNOWN","toStates":[0],"activityNames":["%"]}]}]})",
                            "Unsupported TransitionType");
    }
}

TEST_F(CelonisMatchProcessTest, activity_names_with_embedded_nul) {
    for (const auto* type : {"EXACT_MATCH", "INVERSE_MATCH"}) {
        SCOPED_TRACE(type);
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
        array->append_datum(DatumArray{"a"});
        array->append_datum(DatumArray{Slice("a\0b", 3)});
        array->append_datum(DatumArray{Slice("a\0c", 3)});
        array->append_datum(DatumArray{"ab"});
        const std::string json =
                std::string(R"({"initialState":0,"states":[{"final":false,"transitions":[{"type":")") + type +
                R"(","toStates":[1],"activityNames":["a\u0000b"]}]},{"final":true,"transitions":[]}]})";
        auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
        json_spec->append_datum(Datum{Slice(json)});

        const std::vector<int> expected =
                std::string(type) == "EXACT_MATCH" ? std::vector<int>{0, 1, 0, 0} : std::vector<int>{1, 0, 1, 1};
        match_process({array, json_spec}, expected);
    }
}

TEST_F(CelonisMatchProcessTest, like_pattern_with_embedded_nul) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{"ab"});
    array->append_datum(DatumArray{Slice("a\0b", 3)});
    array->append_datum(DatumArray{Slice("aa\0b", 4)});
    array->append_datum(DatumArray{Slice("a\0c", 3)});
    auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    json_spec->append_datum(Datum{Slice(R"({"initialState":0,"states":[
        {"final":false,"transitions":[{"type":"LIKE","toStates":[1],"activityNames":["a%\u0000b"]}]},
        {"final":true,"transitions":[]}]})")});

    match_process({array, json_spec}, {0, 1, 1, 0});
}

TEST_F(CelonisMatchProcessTest, invalid_activity_names) {
    for (const auto* activity : {"null", "true", "1", "[]", "{}"}) {
        assert_invalid_spec(std::string(R"({"initialState":0,"states":[{"final":false,"transitions":[
                            {"type":"EXACT_MATCH","toStates":[0],"activityNames":["a",)") +
                                    activity + "]}]}]}",
                            "activityNames");
    }
}

TEST_F(CelonisMatchProcessTest, valid_state_boundaries) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    array->append_datum(DatumArray{});
    array->append_datum(DatumArray{"a"});
    array->append_datum(DatumArray{"b"});
    auto json_spec = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    json_spec->append_datum(Datum{Slice(R"({"initialState":1,"states":[
        {"final":false,"transitions":[{"type":"EXACT_MATCH","toStates":[1],"activityNames":["a"]}]},
        {"final":true,"transitions":[{"type":"E_TRANSITION","toStates":[0],"activityNames":[]}]}]})")});

    match_process({array, json_spec}, {1, 1, 0});
}

TEST_F(CelonisMatchProcessTest, celonis_match_b_a) {
    Slice jsonInput(
            "{\n"
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
    Slice jsonInput(
            "{\n"
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
    Slice jsonInput(
            "{\n"
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
    Slice jsonInput(
            "{\n"
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
    Slice jsonInput(
            "{\n"
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

    ASSERT_TRUE(CelonisMatchProcess::match_process_prepare(context, FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                        .ok());

    Columns columns_for_call;
    columns_for_call.push_back(array);
    columns_for_call.push_back(json_spec);
    const auto result = CelonisMatchProcess::celonis_match_process(context, columns_for_call).value();
    ASSERT_EQ(2, result->size());
    EXPECT_TRUE(result->only_null());
    EXPECT_TRUE(result->is_constant());
}

} // namespace starrocks::vectorized
