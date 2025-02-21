#include "exprs/celonis/trim.h"

#include <column/column_viewer.h>
#include <gtest/gtest.h>
#include <oneapi/tbb/detail/_task.h>
#include <util/defer_op.h>

#include <utility>

#include "exprs/anyval_util.h"
#include "util.h"

namespace starrocks {

class CelonisTrimTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto arg_types = {AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type{AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        context_.reset(FunctionContext::create_test_context(std::move(arg_types), std::move(return_type)));
    }

    void TearDown() override {}

    StatusOr<ColumnPtr> RunLtrim(ColumnPtr input_column, ColumnPtr characters) const {
        DeferOp close_fragment_local(
                [this] { CelonisTrim::trim_close(context_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::ltrim_prepare(context_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisTrim::trim_close(context_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::ltrim_prepare(context_.get(), FunctionContext::THREAD_LOCAL));
        const auto result{CelonisTrim::ltrim(context_.get(), {std::move(input_column), std::move(characters)}).value()};
        return std::move(result);
    }

    StatusOr<ColumnPtr> RunRtrim(ColumnPtr input_column, ColumnPtr characters) const {
        DeferOp close_fragment_local(
                [this] { CelonisTrim::trim_close(context_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::rtrim_prepare(context_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisTrim::trim_close(context_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::rtrim_prepare(context_.get(), FunctionContext::THREAD_LOCAL));
        const auto result{CelonisTrim::rtrim(context_.get(), {std::move(input_column), std::move(characters)}).value()};
        return std::move(result);
    }

private:
    std::unique_ptr<FunctionContext> context_;
};

TEST_F(CelonisTrimTest, ltrim_whitespace_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("Example without leading whitespace");
    input_column->append_datum(" Example with leading whitespace");
    input_column->append_datum("          \n PQL");
    input_column->append_datum("a ");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum(" \n");

    const auto whitespace{ColumnHelper::create_const_column<TYPE_VARCHAR>(" ", 1)};

    const auto result{RunLtrim(input_column, whitespace).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 8);
    ASSERT_EQ(result_view.value(0).to_string(), "Example without leading whitespace");
    ASSERT_EQ(result_view.value(1).to_string(), "Example with leading whitespace");
    ASSERT_EQ(result_view.value(2).to_string(), "\n PQL");
    ASSERT_EQ(result_view.value(3).to_string(), "a ");
    ASSERT_EQ(result_view.value(4).to_string(), "");
    ASSERT_EQ(result_view.value(5).to_string(), "");
    ASSERT_TRUE(result->get(6).is_null());
    ASSERT_EQ(result_view.value(7).to_string(), "\n");
}

TEST_F(CelonisTrimTest, ltrim_empty_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("Example without leading whitespace");
    input_column->append_datum(" Example with leading whitespace");
    input_column->append_datum("          \n PQL");
    input_column->append_datum("a ");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum(" \n");

    const auto whitespace{ColumnHelper::create_const_column<TYPE_VARCHAR>("", 1)};

    const auto result{RunLtrim(input_column, whitespace).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 8);
    ASSERT_EQ(result_view.value(0).to_string(), "Example without leading whitespace");
    ASSERT_EQ(result_view.value(1).to_string(), " Example with leading whitespace");
    ASSERT_EQ(result_view.value(2).to_string(), "          \n PQL");
    ASSERT_EQ(result_view.value(3).to_string(), "a ");
    ASSERT_EQ(result_view.value(4).to_string(), " ");
    ASSERT_EQ(result_view.value(5).to_string(), "");
    ASSERT_TRUE(result->get(6).is_null());
    ASSERT_EQ(result_view.value(7).to_string(), " \n");
}

TEST_F(CelonisTrimTest, ltrim_with_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("xyPQL");
    input_column->append_datum("xxPQL");
    input_column->append_datum("zxPQL");
    input_column->append_datum(" PQL");
    input_column->append_nulls(1);

    const auto characters{ColumnHelper::create_const_column<TYPE_VARCHAR>("x", 1)};

    const auto result{RunLtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 5);
    ASSERT_EQ(result_view.value(0).to_string(), "yPQL");
    ASSERT_EQ(result_view.value(1).to_string(), "PQL");
    ASSERT_EQ(result_view.value(2).to_string(), "zxPQL");
    ASSERT_EQ(result_view.value(3).to_string(), " PQL");
    ASSERT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisTrimTest, ltrim_with_null_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("xPQL");
    input_column->append_datum("yPQL");
    input_column->append_datum(" PQL");

    const auto characters{ColumnHelper::create_const_null_column(1)};

    const auto result{RunLtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 3);
    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
    ASSERT_TRUE(result->get(2).is_null());
}

TEST_F(CelonisTrimTest, ltrim_with_trim_args) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("Example without leading whitespace");
    input_column->append_datum(" Example with leading whitespace");
    input_column->append_datum("          \n PQL");
    input_column->append_datum("a ");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum(" \n");

    const auto characters{ColumnHelper::create_const_column<TYPE_VARCHAR>(" \n", 1)};

    const auto result{RunLtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 8);
    ASSERT_EQ(result_view.value(0).to_string(), "Example without leading whitespace");
    ASSERT_EQ(result_view.value(1).to_string(), "Example with leading whitespace");
    ASSERT_EQ(result_view.value(2).to_string(), "PQL");
    ASSERT_EQ(result_view.value(3).to_string(), "a ");
    ASSERT_EQ(result_view.value(4).to_string(), "");
    ASSERT_EQ(result_view.value(5).to_string(), "");
    ASSERT_TRUE(result->get(6).is_null());
    ASSERT_EQ(result_view.value(7).to_string(), "");
}

TEST_F(CelonisTrimTest, ltrim_with_string_column) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("000001");
    input_column->append_datum("_____2");
    input_column->append_datum("_ _ 3");
    input_column->append_datum("!$..4");
    input_column->append_datum("_5");
    input_column->append_datum(" 6");

    const auto characters{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    characters->append_datum("0");
    characters->append_datum("_");
    characters->append_datum(" _");
    characters->append_datum(".!$");
    characters->append_datum("?");
    characters->append_nulls(1);

    const auto result{RunLtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 6);
    ASSERT_EQ(result_view.value(0).to_string(), "1");
    ASSERT_EQ(result_view.value(1).to_string(), "2");
    ASSERT_EQ(result_view.value(2).to_string(), "3");
    ASSERT_EQ(result_view.value(3).to_string(), "4");
    ASSERT_EQ(result_view.value(4).to_string(), "_5");
    ASSERT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisTrimTest, ltrim_all_null_constants) {
    const auto input_column{ColumnHelper::create_const_null_column(TYPE_VARCHAR)};
    const auto characters{ColumnHelper::create_const_null_column(TYPE_VARCHAR)};

    const auto result{RunLtrim(input_column, characters).value()};

    ASSERT_EQ(result->size(), 1);
    ASSERT_TRUE(result->get(0).is_null());
}

TEST_F(CelonisTrimTest, rtrim_whitespace_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("Example without trailing whitespace");
    input_column->append_datum("Example with trailing whitespace ");
    input_column->append_datum("PQL \n");
    input_column->append_datum(" a");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum("\n ");

    const auto whitespace{ColumnHelper::create_const_column<TYPE_VARCHAR>(" ", 1)};

    const auto result{RunRtrim(input_column, whitespace).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 8);
    ASSERT_EQ(result_view.value(0).to_string(), "Example without trailing whitespace");
    ASSERT_EQ(result_view.value(1).to_string(), "Example with trailing whitespace");
    ASSERT_EQ(result_view.value(2).to_string(), "PQL \n");
    ASSERT_EQ(result_view.value(3).to_string(), " a");
    ASSERT_EQ(result_view.value(4).to_string(), "");
    ASSERT_EQ(result_view.value(5).to_string(), "");
    ASSERT_TRUE(result->get(6).is_null());
    ASSERT_EQ(result_view.value(7).to_string(), "\n");
}

TEST_F(CelonisTrimTest, rtrim_empty_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("Example without trailing whitespace");
    input_column->append_datum("Example with trailing whitespace ");
    input_column->append_datum("PQL \n");
    input_column->append_datum(" a");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum("\n ");

    const auto whitespace{ColumnHelper::create_const_column<TYPE_VARCHAR>("", 1)};

    const auto result{RunRtrim(input_column, whitespace).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 8);
    ASSERT_EQ(result_view.value(0).to_string(), "Example without trailing whitespace");
    ASSERT_EQ(result_view.value(1).to_string(), "Example with trailing whitespace ");
    ASSERT_EQ(result_view.value(2).to_string(), "PQL \n");
    ASSERT_EQ(result_view.value(3).to_string(), " a");
    ASSERT_EQ(result_view.value(4).to_string(), " ");
    ASSERT_EQ(result_view.value(5).to_string(), "");
    ASSERT_TRUE(result->get(6).is_null());
    ASSERT_EQ(result_view.value(7).to_string(), "\n ");
}

TEST_F(CelonisTrimTest, rtrim_with_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("PQLyx");
    input_column->append_datum("PQLxx");
    input_column->append_datum("PQLxz");
    input_column->append_datum("PQL ");
    input_column->append_nulls(1);

    const auto characters{ColumnHelper::create_const_column<TYPE_VARCHAR>("x", 1)};

    const auto result{RunRtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 5);
    ASSERT_EQ(result_view.value(0).to_string(), "PQLy");
    ASSERT_EQ(result_view.value(1).to_string(), "PQL");
    ASSERT_EQ(result_view.value(2).to_string(), "PQLxz");
    ASSERT_EQ(result_view.value(3).to_string(), "PQL ");
    ASSERT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisTrimTest, rtrim_with_trim_args) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("Example without trailing whitespace");
    input_column->append_datum("Example with trailing whitespace ");
    input_column->append_datum("PQL    \n");
    input_column->append_datum(" a");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum("\n ");

    const auto characters{ColumnHelper::create_const_column<TYPE_VARCHAR>(" \n", 1)};

    const auto result{RunRtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 8);
    ASSERT_EQ(result_view.value(0).to_string(), "Example without trailing whitespace");
    ASSERT_EQ(result_view.value(1).to_string(), "Example with trailing whitespace");
    ASSERT_EQ(result_view.value(2).to_string(), "PQL");
    ASSERT_EQ(result_view.value(3).to_string(), " a");
    ASSERT_EQ(result_view.value(4).to_string(), "");
    ASSERT_EQ(result_view.value(5).to_string(), "");
    ASSERT_TRUE(result->get(6).is_null());
    ASSERT_EQ(result_view.value(7).to_string(), "");
}

TEST_F(CelonisTrimTest, rtrim_with_null_trim_arg) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("xPQL");
    input_column->append_datum("yPQL");
    input_column->append_datum(" PQL");

    const auto characters{ColumnHelper::create_const_null_column(1)};

    const auto result{RunRtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 3);
    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
    ASSERT_TRUE(result->get(2).is_null());
}

TEST_F(CelonisTrimTest, rtrim_with_string_column) {
    const auto input_column{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    input_column->append_datum("100000");
    input_column->append_datum("2_____");
    input_column->append_datum("3 _ _");
    input_column->append_datum("4..$!");
    input_column->append_datum("5_");
    input_column->append_datum("6 ");

    const auto characters{ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true)};
    characters->append_datum("0");
    characters->append_datum("_");
    characters->append_datum(" _");
    characters->append_datum(".!$");
    characters->append_datum("?");
    characters->append_nulls(1);

    const auto result{RunRtrim(input_column, characters).value()};
    const auto result_view{ColumnViewer<TYPE_VARCHAR>(result)};

    ASSERT_EQ(result->size(), 6);
    ASSERT_EQ(result_view.value(0).to_string(), "1");
    ASSERT_EQ(result_view.value(1).to_string(), "2");
    ASSERT_EQ(result_view.value(2).to_string(), "3");
    ASSERT_EQ(result_view.value(3).to_string(), "4");
    ASSERT_EQ(result_view.value(4).to_string(), "5_");
    ASSERT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisTrimTest, rtrim_all_null_constants) {
    const auto input_column{ColumnHelper::create_const_null_column(TYPE_VARCHAR)};
    const auto characters{ColumnHelper::create_const_null_column(TYPE_VARCHAR)};

    const auto result{RunRtrim(input_column, characters).value()};

    ASSERT_EQ(result->size(), 1);
    ASSERT_TRUE(result->get(0).is_null());
}

} // namespace starrocks