#include "exprs/celonis/like.h"

#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util/defer_op.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisLikeTest : public ::testing::Test {
protected:
    void SetUp() override {
        input_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        pattern_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    void TearDown() override {}

private:
    StatusOr<ColumnPtr> Run(const ColumnPtr& input, const ColumnPtr& pattern) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
        Columns columns;
        columns.push_back(input);
        columns.push_back(pattern);
        ctx->set_constant_columns(columns);
        DeferOp close_fragment_local([&ctx] {
            CelonisLike::like_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisLike::like_prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        DeferOp close_thread_local([&ctx] {
            CelonisLike::like_close(ctx.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisLike::like_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        auto result = CelonisLike::like(ctx.get(), columns);
        return result;
    }

    void RunNonConstantCases() {
        auto result = Run(input_column_, pattern_column_);
        ASSERT_TRUE(result.ok());
        const auto& result_column = result.value();

        auto num_rows = result_column->size();
        ASSERT_EQ(num_rows, expected_.size());

        ColumnViewer<TYPE_VARCHAR> input_viewer(input_column_);
        ColumnViewer<TYPE_VARCHAR> pattern_viewer(pattern_column_);
        ColumnViewer<TYPE_BOOLEAN> result_viewer(result_column);
        for (int row = 0; row < num_rows; row++) {
            EXPECT_EQ(result_viewer.value(row), expected_[row])
                            << "haystack: " << (input_viewer.is_null(row) ? "NULL" : input_viewer.value(row))
                            << ", needle: " << (pattern_viewer.is_null(row) ? "NULL" : pattern_viewer.value(row));
        }
    }

    bool RunOneConstantCase(const Slice& haystack, const Slice& needle) {
        auto input = ColumnHelper::create_const_column<TYPE_VARCHAR>(haystack, 1);
        auto pattern = ColumnHelper::create_const_column<TYPE_VARCHAR>(needle, 1);
        return ColumnHelper::get_const_value<TYPE_BOOLEAN>(Run(input, pattern).value());
    }

    void AddAndRunOneConstantCase(const Slice& haystack, const Slice& needle, bool expected) {
        input_column_->append_datum(haystack);
        pattern_column_->append_datum(needle);
        expected_.push_back(expected);
        EXPECT_EQ(RunOneConstantCase(haystack, needle), expected) << "haystack: " << haystack << ", needle: " << needle;
    }

    ColumnPtr input_column_;
    ColumnPtr pattern_column_;
    std::vector<bool> expected_;
};

TEST_F(CelonisLikeTest, like_wildcard_case_sensitive) {
    AddAndRunOneConstantCase("abcde", "a_c_e", true);
    AddAndRunOneConstantCase("abcde", "a_C_e", false);
    AddAndRunOneConstantCase("abcde", "_b%", true);
    AddAndRunOneConstantCase("abcde", "_B%", false);
    AddAndRunOneConstantCase("abcde", "%e%", true);
    AddAndRunOneConstantCase("abcde", "%E%", false);
    AddAndRunOneConstantCase("äöü", "Ä%", false);
    AddAndRunOneConstantCase("abcdefghijklmnopqrstuvwxyzäöü", "a%äöü", true);

    RunNonConstantCases();
}

TEST_F(CelonisLikeTest, like_case_no_wildcard_case_insensitive) {
    AddAndRunOneConstantCase("abcde", "a", true);
    AddAndRunOneConstantCase("abcde", "A", true);
    AddAndRunOneConstantCase("abcde", "aBcDe", true);
    AddAndRunOneConstantCase("abcde", "B", true);
    AddAndRunOneConstantCase("aBcDe", "AbCdE", true);
    AddAndRunOneConstantCase("abcde", "foo", false);
    AddAndRunOneConstantCase("abcdefghijklmnopqrstuvwxyzäöü", "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ", true);

    RunNonConstantCases();
}

TEST_F(CelonisLikeTest, like_escaped_wildcard) {
    AddAndRunOneConstantCase("_a", R"(\_a)", true);
    AddAndRunOneConstantCase("%a", R"(\%a)", true);
    AddAndRunOneConstantCase("ab", R"(\_b)", false);
    AddAndRunOneConstantCase("ab", R"(\%b)", false);
    AddAndRunOneConstantCase("a", R"(\_)", false);
    AddAndRunOneConstantCase("aba", R"(\%)", false);

    RunNonConstantCases();
}

TEST_F(CelonisLikeTest, like_mixed) {
    AddAndRunOneConstantCase("abcde", "a_c_E", false);
    AddAndRunOneConstantCase("abcde", "BcD", true);
    AddAndRunOneConstantCase("%a_", R"(\%a\_)", true);
    AddAndRunOneConstantCase("äöü", "Ä_Ü", false);

    RunNonConstantCases();
}

TEST_F(CelonisLikeTest, like_multiple_rows_with_constant_wildcard) {
    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    input->append_datum("abcdef");
    input->append_datum("abcd");
    input->append_datum(Datum{});
    input->append_datum("cdefg");
    input->append_datum("foo");

    auto pattern = ColumnHelper::create_const_column<TYPE_VARCHAR>("%cde%", 1);

    const auto result = Run(input, pattern).value();
    EXPECT_EQ(5, result->size());
    EXPECT_TRUE(result->get(0).get_uint8());
    EXPECT_FALSE(result->get(1).get_uint8());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).get_uint8());
    EXPECT_FALSE(result->get(4).get_uint8());
}

TEST_F(CelonisLikeTest, like_multiple_rows_with_constant_no_wildcard) {
    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    input->append_datum("abcde");
    input->append_datum("ABCDE");
    input->append_datum("cDefg");
    input->append_datum("foo");
    input->append_datum(Datum{});

    auto pattern = ColumnHelper::create_const_column<TYPE_VARCHAR>("cde", 1);

    const auto result = Run(input, pattern).value();
    EXPECT_EQ(5, result->size());
    EXPECT_TRUE(result->get(0).get_uint8());
    EXPECT_TRUE(result->get(1).get_uint8());
    EXPECT_TRUE(result->get(2).get_uint8());
    EXPECT_FALSE(result->get(3).get_uint8());
    EXPECT_TRUE(result->get(4).is_null());
}

} // namespace starrocks
