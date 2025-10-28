#include "exprs/celonis/conformance.h"

#include <cpml/conformance/deprecated/conformance_violation_categories.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <testutil/assert.h>

#include "column/column_helper.h"
#include "column/vectorized_fwd.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

const int64_t id_A = 958484639;
const int64_t id_B = 601389851;
const int64_t id_C = 104740979;

class CelonisConformanceTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);

    void evaluate(const Column* result, const Column* expected) {
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

using cpml::conformance::deprecated::make_no_violation_result;
using cpml::conformance::deprecated::make_incomplete_result;
using cpml::conformance::deprecated::make_missing_start_activity_id;
using cpml::conformance::deprecated::make_undesired_activity_result;
using cpml::conformance::deprecated::make_non_conforming_follows_result;

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
    expected->append_datum(DatumArray{make_no_violation_result(), make_no_violation_result()});

    array->append_datum(DatumArray{"A", "C"});
    expected->append_datum(DatumArray{make_no_violation_result(),
                                      /*C is an undesired activity*/ make_undesired_activity_result(id_C)});

    array->append_datum(DatumArray{"A"});
    expected->append_datum(DatumArray{make_incomplete_result()});

    array->append_datum(DatumArray{"A", "A"});
    expected->append_datum(DatumArray{make_no_violation_result(),
                                      /*A is followed by A*/ make_non_conforming_follows_result(id_A, id_A)});

    array->append_datum(DatumArray{"B", "A", "B"});
    expected->append_datum(DatumArray{
            /*B executed as start activity */ cpml::conformance::deprecated::
                    encode_source_and_target_activity_id_in_violation_result({make_missing_start_activity_id(), id_B}),
            make_no_violation_result(), make_no_violation_result()});

    array->append_datum(DatumArray{"A", "C", "A"});
    expected->append_datum(DatumArray{make_no_violation_result(),
                                      /*C is an undesired activity*/ make_undesired_activity_result(id_C),
                                      /*A is followed by A*/ make_non_conforming_follows_result(id_A, id_A)});

    // NULL handling. A NULL value conforms with any Petri net.
    array->append_datum(DatumArray{"A", Datum{}, "B"});
    expected->append_datum(
            DatumArray{make_no_violation_result(), make_no_violation_result(), make_no_violation_result()});

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

    EXPECT_THAT(std::string(conform(input, nullptr).message()), testing::HasSubstr("does not contain 'arcs'."));
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
        // Const array
        int size = 4;
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        array->append_datum(DatumArray{"A", "C"});
        array = ConstColumn::create(array, size);

        auto expected = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
        for (int i = 0; i < size; ++i) {
            expected->append_datum(DatumArray{make_no_violation_result(),
                                              /*C is an undesired activity*/ make_undesired_activity_result(id_C)});
        }

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, size);

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        EXPECT_OK(conform(input, expected.get()));
    }
    {
        // GIVEN
        static const Datum null_v{};
        auto array{ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true)};
        array->append_datum(DatumArray{"A", "B"});
        array->append_datum(DatumArray{null_v, "B"});
        array->append_datum(DatumArray{"A", null_v});
        array->append_datum(DatumArray{null_v, null_v});
        array->append_datum(DatumArray{});
        array->append_datum(DatumArray{"A"});
        array->append_datum(DatumArray{null_v});

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, array->size());

        auto expected{ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true)};
        expected->append_datum(DatumArray{make_no_violation_result(), make_no_violation_result()});
        expected->append_datum(DatumArray{make_no_violation_result(), 57424314137371L});
        expected->append_datum(DatumArray{make_no_violation_result(), make_incomplete_result()});
        expected->append_datum(DatumArray{make_no_violation_result(), make_incomplete_result()});
        expected->append_datum(DatumArray{});
        expected->append_datum(DatumArray{make_incomplete_result()});
        expected->append_datum(DatumArray{make_incomplete_result()});

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        // WHEN - THEN
        EXPECT_OK(conform(input, expected.get()));
    }
}

class CelonisReadableConformanceTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

    void evaluate(const Column* result, const Column* expected) {
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

[[nodiscard]] Slice make_no_violation_readable_result() {
    static const auto value{cpml::conformance::deprecated::make_no_violation_readable_result()};
    return Slice{value};
}

[[nodiscard]] Slice make_incomplete_readable_result() {
    static const auto value{cpml::conformance::deprecated::make_incomplete_readable_result()};
    return Slice{value};
}

[[nodiscard]] Slice make_undesired_activity_readable_result(const std::string_view activity_name) {
    static std::unordered_map<std::string_view, std::string> cache{};
    if (!cache.contains(activity_name)) {
        cache.insert(
                {activity_name, cpml::conformance::deprecated::make_undesired_activity_readable_result(activity_name)});
    }
    return Slice{cache.at(activity_name)};
}

[[nodiscard]] Slice make_missing_start_activity_readable_result(const std::string_view activity_name) {
    static std::unordered_map<std::string_view, std::string> cache{};
    if (!cache.contains(activity_name)) {
        cache.insert({activity_name,
                      cpml::conformance::deprecated::make_missing_start_activity_readable_result(activity_name)});
    }
    return Slice{cache.at(activity_name)};
}

[[nodiscard]] Slice make_non_conforming_follows_readable_result(const std::string_view source_activity_name,
                                                                const std::string_view target_activity_name) {
    static std::unordered_map<std::string, std::string> cache{};
    const std::string concat{"$$$" + std::string{source_activity_name} + "$$$" + std::string{target_activity_name} +
                             "$$$"};
    if (!cache.contains(concat)) {
        cache.insert({concat, cpml::conformance::deprecated::make_non_conforming_follows_readable_result(
                                      source_activity_name, target_activity_name)});
    }
    return Slice{cache.at(concat)};
}

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
    expected->append_datum(DatumArray{make_no_violation_readable_result(), make_no_violation_readable_result()});

    array->append_datum(DatumArray{"A", "C"});
    expected->append_datum(
            DatumArray{make_no_violation_readable_result(), make_undesired_activity_readable_result("C")});

    array->append_datum(DatumArray{"A"});
    expected->append_datum(DatumArray{make_incomplete_readable_result()});

    array->append_datum(DatumArray{"A", "A"});
    expected->append_datum(
            DatumArray{make_no_violation_readable_result(), make_non_conforming_follows_readable_result("A", "A")});

    array->append_datum(DatumArray{"B", "A", "B"});
    expected->append_datum(DatumArray{make_missing_start_activity_readable_result("B"),
                                      make_no_violation_readable_result(), make_no_violation_readable_result()});

    array->append_datum(DatumArray{"A", "C", "A"});
    expected->append_datum(DatumArray{make_no_violation_readable_result(), make_undesired_activity_readable_result("C"),
                                      make_non_conforming_follows_readable_result("A", "A")});

    // NULL handling. A NULL value conforms with any Petri net.
    array->append_datum(DatumArray{"A", Datum{}, "B"});
    expected->append_datum(DatumArray{make_no_violation_readable_result(), make_no_violation_readable_result(),
                                      make_no_violation_readable_result()});

    array->append_datum(DatumArray{"B", "C", "A", "C", Datum{}, "A", "B"});
    expected->append_datum(DatumArray{make_missing_start_activity_readable_result("B"),
                                      make_undesired_activity_readable_result("C"), make_no_violation_readable_result(),
                                      make_undesired_activity_readable_result("C"), make_no_violation_readable_result(),
                                      make_non_conforming_follows_readable_result("A", "A"),
                                      make_no_violation_readable_result()});

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
        // Const array
        int size = 4;
        auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        array->append_datum(DatumArray{"A", "C"});
        array = ConstColumn::create(array, size);

        auto expected = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
        for (int i = 0; i < size; ++i) {
            expected->append_datum(
                    DatumArray{make_no_violation_readable_result(), make_undesired_activity_readable_result("C")});
        }

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, size);

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        EXPECT_OK(conform(input, expected.get()));
    }
    {
        // GIVEN
        static const Datum null_v{};
        int size{11};
        auto array{ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true)};
        array->append_datum(DatumArray{"A", "B"});
        array->append_datum(DatumArray{null_v, "B"});
        array->append_datum(DatumArray{"A", null_v});
        array->append_datum(DatumArray{null_v, null_v});
        array->append_datum(DatumArray{});
        array->append_datum(DatumArray{"A"});
        array->append_datum(DatumArray{null_v});

        auto json_spec = ColumnHelper::create_const_column<TYPE_VARCHAR>(jsonInput, size);

        auto expected{ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true)};
        expected->append_datum(DatumArray{make_no_violation_readable_result(), make_no_violation_readable_result()});
        expected->append_datum(
                DatumArray{make_no_violation_readable_result(), make_missing_start_activity_readable_result("B")});
        expected->append_datum(DatumArray{make_no_violation_readable_result(), make_incomplete_readable_result()});
        expected->append_datum(DatumArray{make_no_violation_readable_result(), make_incomplete_readable_result()});
        expected->append_datum(DatumArray{});
        expected->append_datum(DatumArray{make_incomplete_readable_result()});
        expected->append_datum(DatumArray{make_incomplete_readable_result()});

        Columns input;
        input.push_back(array);
        input.push_back(json_spec);

        // WHEN - THEN
        EXPECT_OK(conform(input, expected.get()));
    }
}

} // namespace starrocks