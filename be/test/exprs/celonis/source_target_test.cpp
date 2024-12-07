#include "exprs/celonis/source_target.h"

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <optional>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

namespace starrocks {

class CelonisSourceTargetTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

    StatusOr<ColumnPtr> run(const TypeDescriptor& array_type_desc, ColumnPtr input, std::optional<ColumnPtr> group,
                            Status (*prepare_fn)(FunctionContext*, FunctionContext::FunctionStateScope),
                            StatusOr<ColumnPtr> (*fn)(FunctionContext*, const Columns&)) {
        auto modifier = ColumnHelper::create_const_column<TYPE_VARCHAR>("any->any", input->size());
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(array_type_desc),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        if (group.has_value()) {
            arg_types.push_back(AnyValUtil::column_type_to_type_desc(TYPE_ARRAY_BIGINT));
        }
        auto return_type = AnyValUtil::column_type_to_type_desc(array_type_desc);
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
        Columns columns;
        columns.push_back(input);
        columns.push_back(modifier);
        if (group.has_value()) {
            columns.push_back(group.value());
        }
        ctx->set_constant_columns(columns);
        RETURN_IF_ERROR(prepare_fn(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        return fn(ctx.get(), columns);
    }

    StatusOr<ColumnPtr> run_celonis_array_sources(const TypeDescriptor& array_type_desc, ColumnPtr input) {
        return run_celonis_array_sources(array_type_desc, std::move(input), std::nullopt);
    }

    StatusOr<ColumnPtr> run_celonis_array_sources(const TypeDescriptor& array_type_desc, ColumnPtr input,
                                                  std::optional<ColumnPtr> group) {
        return run(array_type_desc, std::move(input), std::move(group),
                   &CelonisSourceTargetFunctions::celonis_array_sources_prepare,
                   &CelonisSourceTargetFunctions::celonis_array_sources);
    }

    StatusOr<ColumnPtr> run_celonis_array_targets(const TypeDescriptor& array_type_desc, ColumnPtr input) {
        return run_celonis_array_targets(array_type_desc, std::move(input), std::nullopt);
    }

    StatusOr<ColumnPtr> run_celonis_array_targets(const TypeDescriptor& array_type_desc, ColumnPtr input,
                                                  std::optional<ColumnPtr> group) {
        return run(array_type_desc, std::move(input), std::move(group),
                   &CelonisSourceTargetFunctions::celonis_array_targets_prepare,
                   &CelonisSourceTargetFunctions::celonis_array_targets);
    }

    ColumnPtr modifier_column_;
};

TEST_F(CelonisSourceTargetTest, array_celonis_source) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    celonis::TestEvaluator<TYPE_INT> evaluator_sources;
    celonis::TestEvaluator<TYPE_INT> evaluator_targets;

    array->append_datum(DatumArray{2});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{3, 4});
    evaluator_sources.add_expected(DatumArray{3});
    evaluator_targets.add_expected(DatumArray{4});

    array->append_datum(DatumArray{14, 15, 16});
    evaluator_sources.add_expected(DatumArray{14, 15});
    evaluator_targets.add_expected(DatumArray{15, 16});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_array_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    celonis::TestEvaluator<TYPE_INT> evaluator_sources;
    celonis::TestEvaluator<TYPE_INT> evaluator_targets;

    array->append_datum(DatumArray{2});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{3, 4});
    evaluator_sources.add_expected(DatumArray{3});
    evaluator_targets.add_expected(DatumArray{4});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_array_input_nullable) {
    // Same as above but the input array is Nullable.
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    celonis::TestEvaluator<TYPE_INT> evaluator_sources;
    celonis::TestEvaluator<TYPE_INT> evaluator_targets;

    array->append_datum(DatumArray{2});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{3, 4});
    evaluator_sources.add_expected(DatumArray{3});
    evaluator_targets.add_expected(DatumArray{4});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_input) {
    // Input is empty.
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    ASSERT_EQ(0, result_sources->size());

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    ASSERT_EQ(0, result_targets->size());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_empty_input_nullable) {
    // Same as above, but array is Nullable.
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    ASSERT_EQ(0, result_sources->size());

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    ASSERT_EQ(0, result_targets->size());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_null_in_input) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    celonis::TestEvaluator<TYPE_INT> evaluator_sources;
    celonis::TestEvaluator<TYPE_INT> evaluator_targets;

    array->append_datum(DatumArray{2});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{kNullDatum, 10});
    evaluator_sources.add_expected(DatumArray{kNullDatum});
    evaluator_targets.add_expected(DatumArray{10});

    array->append_datum(DatumArray{10, kNullDatum, kNullDatum});
    evaluator_sources.add_expected(DatumArray{10, kNullDatum});
    evaluator_targets.add_expected(DatumArray{kNullDatum, kNullDatum});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, source_target_const_null_column) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    array->append_datum(DatumArray{2});
    array->append_datum(DatumArray{3});
    auto modifier = ColumnHelper::create_const_null_column(2);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    ctx->set_constant_columns({nullptr, modifier});

    EXPECT_TRUE(CelonisSourceTargetFunctions::celonis_array_sources_prepare(
            ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    EXPECT_TRUE(CelonisSourceTargetFunctions::celonis_array_targets_prepare(
            ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
}

TEST_F(CelonisSourceTargetTest, source_target_const_column) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    array->append_datum(DatumArray{1, 2, 3, 4});
    auto const_array_col = ConstColumn::create(array, 3);

    celonis::TestEvaluator<TYPE_INT> evaluator_sources;
    celonis::TestEvaluator<TYPE_INT> evaluator_targets;

    evaluator_sources.add_expected(DatumArray{1, 2, 3});
    evaluator_targets.add_expected(DatumArray{2, 3, 4});

    evaluator_sources.add_expected(DatumArray{1, 2, 3});
    evaluator_targets.add_expected(DatumArray{2, 3, 4});
    
    evaluator_sources.add_expected(DatumArray{1, 2, 3});
    evaluator_targets.add_expected(DatumArray{2, 3, 4});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, const_array_col).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, const_array_col).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_unsupported_mode) {
    // "any->all" is not supported.
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    array->append_datum(DatumArray{2});
    auto modifier = ColumnHelper::create_const_column<TYPE_VARCHAR>("any->all", array->size());
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    ctx->set_constant_columns({array, modifier});

    EXPECT_TRUE(CelonisSourceTargetFunctions::celonis_array_sources_prepare(
            ctx.get(), FunctionContext::FRAGMENT_LOCAL).is_invalid_argument());
    EXPECT_TRUE(CelonisSourceTargetFunctions::celonis_array_targets_prepare(
            ctx.get(), FunctionContext::FRAGMENT_LOCAL).is_invalid_argument());
}

TEST_F(CelonisSourceTargetTest, array_celonis_source_string_data) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    celonis::TestEvaluator<TYPE_VARCHAR> evaluator_sources;
    celonis::TestEvaluator<TYPE_VARCHAR> evaluator_targets;

    array->append_datum(DatumArray{"string1", "string2"});
    evaluator_sources.add_expected(DatumArray{"string1"});
    evaluator_targets.add_expected(DatumArray{"string2"});

    array->append_datum(DatumArray{kNullDatum, "string3", kNullDatum, "string4"});
    evaluator_sources.add_expected(DatumArray{kNullDatum, "string3", kNullDatum});
    evaluator_targets.add_expected(DatumArray{"string3", kNullDatum, "string4"});

    array->append_datum(DatumArray{"string5"});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, celonis_source_bigint_input) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, false);
    celonis::TestEvaluator<TYPE_BIGINT> evaluator_sources;
    celonis::TestEvaluator<TYPE_BIGINT> evaluator_targets;

    array->append_datum(DatumArray{2000L});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{200000000L, 121L, 30000000L});
    evaluator_sources.add_expected(DatumArray{200000000L, 121L});
    evaluator_targets.add_expected(DatumArray{121L, 30000000L});

    array->append_datum(DatumArray{33L, kNullDatum, 300L});
    evaluator_sources.add_expected(DatumArray{33L, kNullDatum});
    evaluator_targets.add_expected(DatumArray{kNullDatum, 300L});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, celonis_source_null_in_input) {
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    celonis::TestEvaluator<TYPE_BIGINT> evaluator_sources;
    celonis::TestEvaluator<TYPE_BIGINT> evaluator_targets;

    array->append_datum(kNullDatum);
    evaluator_sources.add_expected(kNullDatum);
    evaluator_targets.add_expected(kNullDatum);

    array->append_datum(DatumArray{kNullDatum, kNullDatum});
    evaluator_sources.add_expected(DatumArray{kNullDatum});
    evaluator_targets.add_expected(DatumArray{kNullDatum});

    array->append_datum(DatumArray{});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, array).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, array_celonis_sources_targets_with_group) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    auto group = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    celonis::TestEvaluator<TYPE_INT> evaluator_sources;
    celonis::TestEvaluator<TYPE_INT> evaluator_targets;

    array->append_datum(DatumArray{10, 11, 12, 13, 20, 21, 22, 30, 31});
    group->append_datum(DatumArray{1L, 1L, 1L, 1L, 2L, 2L, 2L, 3L, 3L});
    evaluator_sources.add_expected(DatumArray{10, 11, 12, 20, 21, 30});
    evaluator_targets.add_expected(DatumArray{11, 12, 13, 21, 22, 31});

    array->append_datum(DatumArray{10, 20, 30});
    group->append_datum(DatumArray{1L, 2L, 3L});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    array->append_datum(DatumArray{10, 11, 12, 13});
    group->append_datum(DatumArray{0L, 0L, 0L, 0L});
    evaluator_sources.add_expected(DatumArray{10, 11, 12});
    evaluator_targets.add_expected(DatumArray{11, 12, 13});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, array, group).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_VARCHAR, array, group).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, source_target_const_column_with_group) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    array->append_datum(DatumArray{10, 11, 12, 13, 20, 21, 22, 30, 31});
    auto group = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    group->append_datum(DatumArray{1L, 1L, 1L, 1L, 2L, 2L, 2L, 3L, 3L});
    auto const_array_col = ConstColumn::create(array, 3);
    auto const_group_col = ConstColumn::create(group, 3);

    celonis::TestEvaluator<TYPE_INT> evaluator_sources;
    celonis::TestEvaluator<TYPE_INT> evaluator_targets;

    evaluator_sources.add_expected(DatumArray{10, 11, 12, 20, 21, 30});
    evaluator_targets.add_expected(DatumArray{11, 12, 13, 21, 22, 31});

    evaluator_sources.add_expected(DatumArray{10, 11, 12, 20, 21, 30});
    evaluator_targets.add_expected(DatumArray{11, 12, 13, 21, 22, 31});

    evaluator_sources.add_expected(DatumArray{10, 11, 12, 20, 21, 30});
    evaluator_targets.add_expected(DatumArray{11, 12, 13, 21, 22, 31});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_INT, const_array_col, const_group_col).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_INT, const_array_col, const_group_col).value();
    evaluator_targets.evaluate(result_targets);
}

TEST_F(CelonisSourceTargetTest, array_celonis_sources_targets_with_group_string_data) {
    auto array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    auto group = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    celonis::TestEvaluator<TYPE_VARCHAR> evaluator_sources;
    celonis::TestEvaluator<TYPE_VARCHAR> evaluator_targets;

    array->append_datum(DatumArray{"s00", "s01", "s02", "s03", "s04", "s15", "s16", "s27", "s28", "s29"});
    group->append_datum(DatumArray{0L, 0L, 0L, 0L, 0L, 1L, 1L, 2L, 2L, 2L});
    evaluator_sources.add_expected(DatumArray{"s00", "s01", "s02", "s03", "s15", "s27", "s28"});
    evaluator_targets.add_expected(DatumArray{"s01", "s02", "s03", "s04", "s16", "s28", "s29"});

    array->append_datum(DatumArray{"s00", "s01", "s12", "s13", "s04", "s15", "s06", "s17", "s28", "s09"});
    group->append_datum(DatumArray{0L, 0L, 1L, 1L, 0L, 1L, 0L, 1L, 2L, 0L});
    evaluator_sources.add_expected(DatumArray{"s00", "s01", "s04", "s06", "s12", "s13", "s15"});
    evaluator_targets.add_expected(DatumArray{"s01", "s04", "s06", "s09", "s13", "s15", "s17"});

    // If input_arrays is NULL, the result is NULL regardless of group_array.
    array->append_datum(kNullDatum);
    group->append_datum(DatumArray{0L, 0L, 0L, 0L, 0L, 1L, 1L, 2L, 2L, 2L});
    evaluator_sources.add_expected(kNullDatum);
    evaluator_targets.add_expected(kNullDatum);

    // Empty input_array
    array->append_datum(DatumArray{});
    group->append_datum(DatumArray{0L, 0L, 0L, 0L, 0L, 1L, 1L, 2L, 2L, 2L});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    // NULL group_array
    array->append_datum(DatumArray{"s00", "s01", "s12", "s13", "s04", "s15", "s06", "s17", "s28", "s09"});
    group->append_datum(kNullDatum);
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    // NULL in group_array
    array->append_datum(DatumArray{"s00", "s01", "s12", "s13", "s04", "s15", "s06", "s17", "s28", "s09"});
    group->append_datum(DatumArray{0L, 0L, 1L, 1L, kNullDatum, 1L, 0L, 1L, 2L, 0L});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    // Mismatched sizes of input_array and group_array
    array->append_datum(DatumArray{"s00", "s01", "s12", "s13", "s04", "s15", "s06", "s17", "s28", "s09"});
    group->append_datum(DatumArray{0L, 0L, 1L, 1L, 1L});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    // One element per group
    array->append_datum(DatumArray{"s00", "s11", "s22"});
    group->append_datum(DatumArray{0L, 1L, 2L});
    evaluator_sources.add_expected(DatumArray{});
    evaluator_targets.add_expected(DatumArray{});

    // One group
    array->append_datum(DatumArray{"s00", "s01", "s02", "s03"});
    group->append_datum(DatumArray{0L, 0L, 0L, 0L});
    evaluator_sources.add_expected(DatumArray{"s00", "s01", "s02"});
    evaluator_targets.add_expected(DatumArray{"s01", "s02", "s03"});

    // Null in input_array
    array->append_datum(DatumArray{"string00", kNullDatum, "s02", "s03", "s04", kNullDatum, "s16"});
    group->append_datum(DatumArray{0L, 0L, 0L, 0L, 0L, 1L, 1L});
    evaluator_sources.add_expected(DatumArray{"string00", kNullDatum, "s02", "s03", kNullDatum});
    evaluator_targets.add_expected(DatumArray{kNullDatum, "s02", "s03", "s04", "s16"});

    auto result_sources = run_celonis_array_sources(TYPE_ARRAY_VARCHAR, array, group).value();
    evaluator_sources.evaluate(result_sources);

    auto result_targets = run_celonis_array_targets(TYPE_ARRAY_VARCHAR, array, group).value();
    evaluator_targets.evaluate(result_targets);
}

} // namespace starrocks
