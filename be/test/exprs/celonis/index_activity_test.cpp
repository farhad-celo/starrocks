#include "exprs/celonis/index_activity.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisIndexActivityTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

private:
    StatusOr<ColumnPtr> run(const TypeDescriptor& array_type_desc, const std::string& mode,
                            const std::string& direction, ColumnPtr input) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                array_type_desc,
                TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
        auto return_type = TYPE_ARRAY_BIGINT;
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

        Columns columns;
        columns.push_back(input);
        columns.push_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(mode, input->size()));
        columns.push_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(direction, input->size()));
        ctx->set_constant_columns(columns);

        DeferOp op([&ctx] {
            CelonisIndexActivity::celonis_index_activity_close(
                    ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
        });

        RETURN_IF_ERROR(CelonisIndexActivity::celonis_index_activity_prepare(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        ASSIGN_OR_RETURN(auto result, CelonisIndexActivity::celonis_index_activity(ctx.get(), columns));
        return result;
    }

    void run_test(const TypeDescriptor& array_type_desc, const std::string& mode,
                  const std::string& direction, ColumnPtr input) {
        auto result = run(array_type_desc, mode, direction, std::move(input));
        ASSERT_TRUE(result.ok()) << result.status().message();
        evaluator_.evaluate(result.value());
    }

    celonis::TestEvaluator<TYPE_BIGINT> evaluator_;
};

TEST_F(CelonisIndexActivityTest, const_null_column_mode_direction) {
    auto array = ColumnHelper::create_const_null_column(2);
    auto mode_column = ColumnHelper::create_const_null_column(2);
    auto direction_column = ColumnHelper::create_const_null_column(2);

    std::vector<FunctionContext::TypeDesc> arg_types = {
            TYPE_ARRAY_INT,
            TypeDescriptor::from_logical_type(TYPE_VARCHAR),
            TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
    auto return_type = TYPE_ARRAY_BIGINT;
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    Columns columns;
    columns.push_back(array);
    columns.push_back(mode_column);
    columns.push_back(direction_column);
    ctx->set_constant_columns(columns);

    DeferOp op([&ctx] {
        CelonisIndexActivity::celonis_index_activity_close(
                ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL);
    });

    ASSERT_TRUE(CelonisIndexActivity::celonis_index_activity_prepare(
            ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());
}

TEST_F(CelonisIndexActivityTest, index_activity_order_empty_input) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);

    array->append_datum(DatumArray{});
    evaluator_.add_expected(DatumArray{});

    array->append_datum(DatumArray{kNullDatum});
    evaluator_.add_expected(DatumArray{kNullDatum});

    array->append_datum(kNullDatum);
    evaluator_.add_expected(kNullDatum);

    run_test(TYPE_ARRAY_INT, "INDEX_ACTIVITY_ORDER", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_order_nulls) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);

    array->append_datum(DatumArray{2});
    evaluator_.add_expected(DatumArray{1L});

    array->append_datum(DatumArray{kNullDatum, 10});
    evaluator_.add_expected(DatumArray{kNullDatum, 1L});

    array->append_datum(DatumArray{10, kNullDatum, 10, 20, 10});
    evaluator_.add_expected(DatumArray{1L, kNullDatum, 2L, 3L, 4L});

    run_test(TYPE_ARRAY_INT, "INDEX_ACTIVITY_ORDER", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_order_non_null_input) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);

    array->append_datum(DatumArray{2});
    evaluator_.add_expected(DatumArray{1L});

    array->append_datum(DatumArray{10, 20, 10});
    evaluator_.add_expected(DatumArray{1L, 2L, 3L});

    run_test(TYPE_ARRAY_INT, "INDEX_ACTIVITY_ORDER", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_order_reverse_nulls) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B", kNullDatum});
    evaluator_.add_expected(DatumArray{2L, 1L, kNullDatum});

    array->append_datum(DatumArray{"A", kNullDatum});
    evaluator_.add_expected(DatumArray{1L, kNullDatum});

    array->append_datum(DatumArray{"A", kNullDatum, "D", kNullDatum});
    evaluator_.add_expected(DatumArray{2L, kNullDatum, 1L, kNullDatum});

    run_test(TYPE_ARRAY_INT, "INDEX_ACTIVITY_ORDER", "REVERSE", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_order_reverse_non_null_input) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B", "B"});
    evaluator_.add_expected(DatumArray{3L, 2L, 1L});

    array->append_datum(DatumArray{"A", "B"});
    evaluator_.add_expected(DatumArray{2L, 1L});

    run_test(TYPE_ARRAY_INT, "INDEX_ACTIVITY_ORDER", "REVERSE", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_loop_empty_input) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);

    array->append_datum(DatumArray{});
    evaluator_.add_expected(DatumArray{});

    array->append_datum(DatumArray{kNullDatum});
    evaluator_.add_expected(DatumArray{kNullDatum});

    array->append_datum(kNullDatum);
    evaluator_.add_expected(kNullDatum);

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_LOOP", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_loop_varchar) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B", "A", "A", "A"});
    evaluator_.add_expected(DatumArray{1L, 1L, 1L, 2L, 3L});

    array->append_datum(DatumArray{"A", "B", "B", "B", "Bob"});
    evaluator_.add_expected(DatumArray{1L, 1L, 2L, 3L, 1L});

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_LOOP", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_loop_nulls) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, false);

    array->append_datum(DatumArray{10L, kNullDatum, 10L, kNullDatum, 10L, kNullDatum, 20L, 10L});
    evaluator_.add_expected(DatumArray{1L, kNullDatum, 2L, kNullDatum, 3L, kNullDatum, 1L, 1L});

    array->append_datum(DatumArray{10L, kNullDatum, kNullDatum, kNullDatum, 10L, -10L});
    evaluator_.add_expected(DatumArray{1L, kNullDatum, kNullDatum, kNullDatum, 2L, 1L});

    array->append_datum(DatumArray{kNullDatum, kNullDatum, 10L, 10L});
    evaluator_.add_expected(DatumArray{kNullDatum, kNullDatum, 1L, 2L});

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_LOOP", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_loop_reverse_varchar) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B", "A", "A", "A"});
    evaluator_.add_expected(DatumArray{1L, 1L, 3L, 2L, 1L});

    array->append_datum(DatumArray{"A", "B", "B", "B", "Bob"});
    evaluator_.add_expected(DatumArray{1L, 3L, 2L, 1L, 1L});

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_LOOP", "REVERSE", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_loop_reverse_nulls) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, false);

    array->append_datum(DatumArray{10L, kNullDatum, 10L, kNullDatum, 10L, kNullDatum, 20L, 10L});
    evaluator_.add_expected(DatumArray{3L, kNullDatum, 2L, kNullDatum, 1L, kNullDatum, 1L, 1L});

    array->append_datum(DatumArray{10L, kNullDatum, kNullDatum, kNullDatum, 10L, -10L});
    evaluator_.add_expected(DatumArray{2L, kNullDatum, kNullDatum, kNullDatum, 1L, 1L});

    array->append_datum(DatumArray{kNullDatum, kNullDatum, 10L, 10L});
    evaluator_.add_expected(DatumArray{kNullDatum, kNullDatum, 2L, 1L});

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_LOOP", "REVERSE", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_type_empty_input) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);

    array->append_datum(DatumArray{});
    evaluator_.add_expected(DatumArray{});

    array->append_datum(DatumArray{kNullDatum});
    evaluator_.add_expected(DatumArray{kNullDatum});

    array->append_datum(kNullDatum);
    evaluator_.add_expected(kNullDatum);

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_TYPE", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_type_varchar) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B", "A", "A", "A"});
    evaluator_.add_expected(DatumArray{1L, 1L, 2L, 3L, 4L});

    array->append_datum(DatumArray{"A", "B", "B", "B", "C"});
    evaluator_.add_expected(DatumArray{1L, 1L, 2L, 3L, 1L});

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_TYPE", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_type_nulls) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);

    array->append_datum(DatumArray{2});
    evaluator_.add_expected(DatumArray{1L});

    array->append_datum(DatumArray{kNullDatum, 10});
    evaluator_.add_expected(DatumArray{kNullDatum, 1L});

    array->append_datum(DatumArray{10, kNullDatum, 10, 20, kNullDatum, 10});
    evaluator_.add_expected(DatumArray{1L, kNullDatum, 2L, 1L, kNullDatum, 3L});

    run_test(TYPE_ARRAY_INT, "INDEX_ACTIVITY_TYPE", "FORWARD", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_type_reverse_varchar) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);

    array->append_datum(DatumArray{"A", "B", "A", "A", "A"});
    evaluator_.add_expected(DatumArray{4L, 1L, 3L, 2L, 1L});

    array->append_datum(DatumArray{"A", "B", "B", "B", "C"});
    evaluator_.add_expected(DatumArray{1L, 3L, 2L, 1L, 1L});

    array->append_datum(DatumArray{"A", "B", "A", "B", "A", "A", "C"});
    evaluator_.add_expected(DatumArray{4L, 2L, 3L, 1L, 2L, 1L, 1L});

    run_test(TYPE_ARRAY_VARCHAR, "INDEX_ACTIVITY_TYPE", "REVERSE", array);
}

TEST_F(CelonisIndexActivityTest, index_activity_type_reverse_nulls) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);

    array->append_datum(DatumArray{2});
    evaluator_.add_expected(DatumArray{1L});

    array->append_datum(DatumArray{kNullDatum, 10});
    evaluator_.add_expected(DatumArray{kNullDatum, 1L});

    array->append_datum(DatumArray{10, kNullDatum, 10, 20, kNullDatum, 10});
    evaluator_.add_expected(DatumArray{3L, kNullDatum, 2L, 1L, kNullDatum, 1L});

    run_test(TYPE_ARRAY_INT, "INDEX_ACTIVITY_TYPE", "REVERSE", array);
}

TEST_F(CelonisIndexActivityTest, unsupported_mode_direction) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);

    array->append_datum(DatumArray{2});

    EXPECT_TRUE(run(TYPE_ARRAY_INT, "INDEX_ACTIVITY_TYPE", "INVALID", array).status().is_invalid_argument());
    EXPECT_TRUE(run(TYPE_ARRAY_INT, "INDEX_ACTIVITY_INVALID", "REVERSE", array).status().is_invalid_argument());
    EXPECT_TRUE(run(TYPE_ARRAY_INT, "", "", array).status().is_invalid_argument());
}

} // namespace starrocks
