#include "exprs/celonis/array_trimmed_mean.h"

#include <gtest/gtest.h>

#include "exprs/anyval_util.h"
#include "util.h"

namespace starrocks {
class CelonisArrayTrimmedMeanTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor TYPE_ARRAY_INT = celonis::array_type(TYPE_INT);
    TypeDescriptor TYPE_ARRAY_DOUBLE = celonis::array_type(TYPE_DOUBLE);
    TypeDescriptor TYPE_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);

private:
    static std::unique_ptr<FunctionContext> create_context(LogicalType elementType) {
        std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(elementType),
                                                            TypeDescriptor::from_logical_type(TYPE_BIGINT),
                                                            TypeDescriptor::from_logical_type(TYPE_BIGINT)};
        auto return_type = TypeDescriptor::from_logical_type(TYPE_DOUBLE);
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), std::move(return_type)));
    }
};

TEST_F(CelonisArrayTrimmedMeanTest, null_input_array) {
    auto local_ctx{create_context(TYPE_INT)};
    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 2);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 2);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
    auto input_array = ColumnHelper::create_const_null_column(2);

    const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                .value();
    ASSERT_EQ(2, result->size());
    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisArrayTrimmedMeanTest, null_lower_cutoff) {
    auto local_ctx{create_context(TYPE_INT)};
    auto const_column_lower = ColumnHelper::create_const_null_column(2);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 2);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{2});
    input_array->append_datum(DatumArray{1, 2, 3});

    const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                .value();
    ASSERT_EQ(2, result->size());
    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisArrayTrimmedMeanTest, null_upper_cutoff) {
    auto local_ctx{create_context(TYPE_INT)};
    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 2);
    auto const_column_upper = ColumnHelper::create_const_null_column(2);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{2});
    input_array->append_datum(DatumArray{1, 2, 3});

    const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                .value();
    ASSERT_EQ(2, result->size());
    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisArrayTrimmedMeanTest, cut_all) {
    auto local_ctx{create_context(TYPE_INT)};
    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(70, 2);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(70, 2);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{2});
    input_array->append_datum(DatumArray{1, 2, 3, 1, 2, 3, 4});
    input_array->append_datum(DatumArray{1, 2, 3, 1, 2, 3, 4, kNullDatum});

    const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
            local_ctx.get(), {input_array, const_column_lower, const_column_upper});
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(),
              "CELONIS_ARRAY_TRIMMED_MEAN: Sum of lower cutoff and upper cutoff must be in interval [0, 100].");
}

TEST_F(CelonisArrayTrimmedMeanTest, invalid_cutoff) {
    {
        auto local_ctx{create_context(TYPE_INT)};
        auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(120, 2);
        auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 2);
        local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{2});
        input_array->append_datum(DatumArray{1, 2, 3, 1, 2, 3, 4});

        const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                local_ctx.get(), {input_array, const_column_lower, const_column_upper});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "CELONIS_ARRAY_TRIMMED_MEAN: Cutoff value must be in interval [0, 100].");
    }
    {
        auto local_ctx{create_context(TYPE_INT)};
        auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 2);
        auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(120, 2);
        local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{2});
        input_array->append_datum(DatumArray{1, 2, 3, 1, 2, 3, 4});

        const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                local_ctx.get(), {input_array, const_column_lower, const_column_upper});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(), "CELONIS_ARRAY_TRIMMED_MEAN: Cutoff value must be in interval [0, 100].");
    }
    {
        auto local_ctx{create_context(TYPE_INT)};
        auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(49, 2);
        auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(52, 2);
        local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
        auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
        input_array->append_datum(DatumArray{2});
        input_array->append_datum(DatumArray{1, 2, 3, 1, 2, 3, 4});

        const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                local_ctx.get(), {input_array, const_column_lower, const_column_upper});
        ASSERT_TRUE(result.status().is_invalid_argument());
        EXPECT_EQ(result.status().message(),
                  "CELONIS_ARRAY_TRIMMED_MEAN: Sum of lower cutoff and upper cutoff must be in interval [0, 100].");
    }
}

TEST_F(CelonisArrayTrimmedMeanTest, simple_example) {
    auto local_ctx{create_context(TYPE_INT)};
    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{});
    input_array->append_datum(DatumArray{2});
    input_array->append_datum(DatumArray{1, 2, 3});
    input_array->append_datum(DatumArray{3, 2, 1});
    input_array->append_datum(DatumArray{2, 1, 3});
    input_array->append_datum(DatumArray{102, 101, 100, 4, 3, 2, 1, -100, -101, -102});

    const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                .value();

    ASSERT_EQ(6, result->size());

    ASSERT_EQ(0, result->get(0).get_double());
    ASSERT_EQ(2, result->get(1).get_double());
    ASSERT_EQ(2, result->get(2).get_double());
    ASSERT_EQ(2, result->get(3).get_double());
    ASSERT_EQ(2, result->get(4).get_double());
    ASSERT_EQ(2.5, result->get(5).get_double());
}

TEST_F(CelonisArrayTrimmedMeanTest, second_simple_example) {
    auto local_ctx{create_context(TYPE_INT)};
    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    input_array->append_datum(DatumArray{});
    input_array->append_datum(DatumArray{2});
    input_array->append_datum(DatumArray{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    input_array->append_datum(DatumArray{10, 2, 10, 3, 40, 4, 22, 5, 3, 43});
    input_array->append_datum(DatumArray{102, 101, 100, 4, 3, 2, 1, -100, -101, -102});

    const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                .value();
    ASSERT_EQ(5, result->size());

    ASSERT_EQ(0, result->get(0).get_double());
    ASSERT_EQ(2, result->get(1).get_double());
    ASSERT_EQ(5.5, result->get(2).get_double());
    ASSERT_EQ(14.2, result->get(3).get_double());
    ASSERT_EQ(1, result->get(4).get_double());
}

TEST_F(CelonisArrayTrimmedMeanTest, null_values_in_array) {
    auto local_ctx{create_context(TYPE_INT)};

    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, true);
    input_array->append_datum(DatumArray{kNullDatum});
    input_array->append_datum(kNullDatum);
    input_array->append_datum(DatumArray{1, kNullDatum, 2, 3});
    input_array->append_datum(DatumArray{kNullDatum, kNullDatum, kNullDatum});

    const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                .value();
    ASSERT_EQ(4, result->size());

    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
    ASSERT_EQ(2, result->get(2).get_double());
    ASSERT_TRUE(result->get(3).is_null());
}

TEST_F(CelonisArrayTrimmedMeanTest, different_trims) {
    auto input_array = ColumnHelper::create_column(TYPE_ARRAY_INT, false);
    // sorted: 2, 3, 3, 4, 5, 10, 10, 22, 40, 43
    input_array->append_datum(DatumArray{10, 2, 10, 3, 40, 4, 22, 5, 3, 43});

    {
        auto local_ctx{create_context(TYPE_INT)};
        auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
        auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
        local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

        const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                    local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                    .value();
        ASSERT_EQ(1, result->size());
        ASSERT_EQ(14.2, result->get(0).get_double());
    }
    {
        auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 1);
        auto local_ctx{create_context(TYPE_INT)};
        auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
        local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
        const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                    local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                    .value();
        ASSERT_EQ(1, result->size());
        EXPECT_NEAR(19.1429, result->get(0).get_double(), 0.01);
    }
    {
        auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(2, 1);
        auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(30, 1);
        auto local_ctx{create_context(TYPE_INT)};
        local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});
        const auto result = CelonisArrayTrimmedMean<TYPE_INT>::celonis_array_trimmed_mean(
                                    local_ctx.get(), {input_array, const_column_lower, const_column_upper})
                                    .value();
        ASSERT_EQ(1, result->size());
        EXPECT_NEAR(5.28, result->get(0).get_double(), 0.01);
    }
}

TEST_F(CelonisArrayTrimmedMeanTest, bigint_array) {
    auto local_ctx{create_context(TYPE_BIGINT)};
    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1L, kNullDatum, 2L, 3L});
    arrays->append_datum(DatumArray{kNullDatum, 2L, kNullDatum, 3L, kNullDatum, 2L});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{100L, 100L});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, -7L, 7L, 7L});
    const auto result = CelonisArrayTrimmedMean<TYPE_BIGINT>::celonis_array_trimmed_mean(
                                local_ctx.get(), {arrays, const_column_lower, const_column_upper})
                                .value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(0.0, result->get(0).get_double());
    ASSERT_TRUE(result->get(1).is_null());
    EXPECT_EQ(2.0, result->get(2).get_double());
    EXPECT_NEAR(2.33, result->get(3).get_double(), 0.01);
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(100.0, result->get(5).get_double());
    EXPECT_NEAR(2.33, result->get(6).get_double(), 0.01);
}

TEST_F(CelonisArrayTrimmedMeanTest, double_array) {
    auto local_ctx{create_context(TYPE_DOUBLE)};
    auto const_column_lower = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_BIGINT>(5, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

    auto arrays = ColumnHelper::create_column(TYPE_ARRAY_DOUBLE, true);
    arrays->append_datum(DatumArray{});
    arrays->append_datum(kNullDatum);
    arrays->append_datum(DatumArray{kNullDatum, 1.2, kNullDatum, 2.5, 1.2});
    arrays->append_datum(DatumArray{kNullDatum, -2.3, kNullDatum, 3.0, kNullDatum, 3.0});
    arrays->append_datum(DatumArray{kNullDatum});
    arrays->append_datum(DatumArray{-120.3, 120.3});
    arrays->append_datum(DatumArray{kNullDatum, kNullDatum, 3.14, 3.14, 5.3});
    const auto result = CelonisArrayTrimmedMean<TYPE_DOUBLE>::celonis_array_trimmed_mean(
                                local_ctx.get(), {arrays, const_column_lower, const_column_upper})
                                .value();
    EXPECT_EQ(7, result->size());
    EXPECT_EQ(0L, result->get(0).get_double());
    ASSERT_TRUE(result->get(1).is_null());
    EXPECT_NEAR(1.63, result->get(2).get_double(), 0.01);
    EXPECT_NEAR(1.23, result->get(3).get_double(), 0.01);
    EXPECT_TRUE(result->get(4).is_null());
    EXPECT_EQ(0.0, result->get(5).get_double());
    EXPECT_EQ(3.86, result->get(6).get_double());
}
} // namespace starrocks