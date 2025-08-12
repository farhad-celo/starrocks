#include "exprs/celonis/like.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisLikeTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    StatusOr<ColumnPtr> Run(ColumnPtr input, ColumnPtr pattern) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
        Columns columns;
        columns.push_back(input);
        columns.push_back(pattern);
        ctx->set_constant_columns(columns);
        RETURN_IF_ERROR(CelonisLike::like_prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        RETURN_IF_ERROR(CelonisLike::like_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        auto result = CelonisLike::like(ctx.get(), columns);
        RETURN_IF_ERROR(CelonisLike::like_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
        RETURN_IF_ERROR(CelonisLike::like_close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        return result;
    }

    bool RunOneCase(const std::string& haystack, const std::string& needle) {
        auto input = ColumnHelper::create_const_column<TYPE_VARCHAR>(haystack, 1);
        auto pattern = ColumnHelper::create_const_column<TYPE_VARCHAR>(needle, 1);
        return ColumnHelper::get_const_value<TYPE_BOOLEAN>(Run(input, pattern).value());
    }
};

TEST_F(CelonisLikeTest, like_wildcard_case_sensitive) {
    EXPECT_TRUE(RunOneCase("abcde", "a_c_e"));
    EXPECT_FALSE(RunOneCase("abcde", "a_C_e"));
    EXPECT_TRUE(RunOneCase("abcde", "_b%"));
    EXPECT_FALSE(RunOneCase("abcde", "_B%"));
    EXPECT_TRUE(RunOneCase("abcde", "%e%"));
    EXPECT_FALSE(RunOneCase("abcde", "%E%"));
}

TEST_F(CelonisLikeTest, like_case_no_wildcard_case_insensitive) {
    EXPECT_TRUE(RunOneCase("abcde", "a"));
    EXPECT_TRUE(RunOneCase("abcde", "A"));
    EXPECT_TRUE(RunOneCase("abcde", "aBcDe"));
    EXPECT_TRUE(RunOneCase("abcde", "B"));
    EXPECT_TRUE(RunOneCase("aBcDe", "AbCdE"));
    EXPECT_FALSE(RunOneCase("abcde", "foo"));
}

TEST_F(CelonisLikeTest, like_escaped_wildcard) {
    EXPECT_TRUE(RunOneCase("_a", R"(\_a)"));
    EXPECT_TRUE(RunOneCase("%a", R"(\%a)"));
    EXPECT_FALSE(RunOneCase("ab", R"(\_b)"));
    EXPECT_FALSE(RunOneCase("ab", R"(\%b)"));
    EXPECT_FALSE(RunOneCase("a", R"(\_)"));
    EXPECT_FALSE(RunOneCase("aba", R"(\%)"));
}

TEST_F(CelonisLikeTest, like_wildcard_multiple_rows) {
    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    input->append_datum("abcdef");
    input->append_datum("abcd");
    input->append_datum(Datum{});
    input->append_datum("cdefg");
    input->append_datum("foo");

    auto pattern = ColumnHelper::create_const_column<TYPE_VARCHAR>("%cde%", 1);

    const auto result = Run(input, pattern).value();
    EXPECT_EQ(5, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_EQ(true, result->get(3).get_uint8());
    EXPECT_EQ(false, result->get(4).get_uint8());
}

TEST_F(CelonisLikeTest, like_no_wildcard_multiple_rows) {
    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    input->append_datum("abcde");
    input->append_datum("ABCDE");
    input->append_datum("cDefg");
    input->append_datum("foo");
    input->append_datum(Datum{});

    auto pattern = ColumnHelper::create_const_column<TYPE_VARCHAR>("cde", 1);

    const auto result = Run(input, pattern).value();
    EXPECT_EQ(5, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(true, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
    EXPECT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisLikeTest, null_pattern) {
    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    input->append_datum("foo");

    auto pattern = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    pattern->append_datum(Datum{});

    auto result = Run(input, pattern);

    EXPECT_TRUE(result.status().is_not_supported());
}

} // namespace starrocks
