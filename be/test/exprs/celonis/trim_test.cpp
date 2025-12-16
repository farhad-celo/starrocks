#include "exprs/celonis/trim.h"

#include <column/column_viewer.h>
#include <gtest/gtest.h>
#include <util/defer_op.h>

#include <utility>

#include "exprs/celonis/anyval_util.h"

namespace starrocks {

class CelonisTrimTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto arg_types = {CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
        context_.reset(FunctionContext::create_test_context(std::move(arg_types), std::move(return_type)));
    }

    void TearDown() override {}

    StatusOr<ColumnPtr> RunLtrimConstantCase(ColumnPtr input_column, ColumnPtr characters) const {
        Columns constant_columns;
        constant_columns.push_back(nullptr);
        constant_columns.push_back(characters);
        context_->set_constant_columns(constant_columns);
        return RunLtrim(input_column, characters);
    }

    StatusOr<ColumnPtr> RunLtrimNonConstantCase(ColumnPtr input_column, ColumnPtr characters) const {
        return RunLtrim(input_column, characters);
    }

    StatusOr<ColumnPtr> RunRtrimConstantCase(ColumnPtr input_column, ColumnPtr characters) const {
        Columns constant_columns;
        constant_columns.push_back(nullptr);
        constant_columns.push_back(characters);
        context_->set_constant_columns(constant_columns);
        return RunRtrim(input_column, characters);
    }

    StatusOr<ColumnPtr> RunRtrimNonConstantCase(ColumnPtr input_column, ColumnPtr characters) const {
        return RunRtrim(input_column, characters);
    }

private:
    StatusOr<ColumnPtr> RunLtrim(ColumnPtr input_column, ColumnPtr characters) const {
        DeferOp close_fragment_local(
                [this] { CelonisTrim::trim_close(context_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::ltrim_prepare(context_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisTrim::trim_close(context_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::ltrim_prepare(context_.get(), FunctionContext::THREAD_LOCAL));
        Columns columns;
        columns.push_back(std::move(input_column));
        columns.push_back(std::move(characters));
        auto result_status = CelonisTrim::ltrim(context_.get(), columns);
        RETURN_IF_ERROR(result_status);
        return result_status.value();
    }

    StatusOr<ColumnPtr> RunRtrim(ColumnPtr input_column, ColumnPtr characters) const {
        DeferOp close_fragment_local(
                [this] { CelonisTrim::trim_close(context_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::rtrim_prepare(context_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisTrim::trim_close(context_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisTrim::rtrim_prepare(context_.get(), FunctionContext::THREAD_LOCAL));
        Columns columns;
        columns.push_back(std::move(input_column));
        columns.push_back(std::move(characters));
        auto result_status = CelonisTrim::rtrim(context_.get(), columns);
        RETURN_IF_ERROR(result_status);
        return result_status.value();
    }

    std::unique_ptr<FunctionContext> context_;
};

TEST_F(CelonisTrimTest, ltrim_whitespace_trim_arg) {
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("Example without leading whitespace");
    input_column->append_datum(" Example with leading whitespace");
    input_column->append_datum("          \n PQL");
    input_column->append_datum("a ");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum(" \n");

    ColumnPtr whitespace = ColumnHelper::create_const_column<TYPE_VARCHAR>(" ", input_column->size());

    auto result_status = RunLtrimConstantCase(input_column, whitespace);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

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
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("Example without leading whitespace");
    input_column->append_datum(" Example with leading whitespace");
    input_column->append_datum("          \n PQL");
    input_column->append_datum("a ");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum(" \n");

    ColumnPtr whitespace = ColumnHelper::create_const_column<TYPE_VARCHAR>("", input_column->size());

    auto result_status = RunLtrimConstantCase(input_column, whitespace);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

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
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("xyPQL");
    input_column->append_datum("xxPQL");
    input_column->append_datum("zxPQL");
    input_column->append_datum(" PQL");
    input_column->append_nulls(1);

    ColumnPtr characters = ColumnHelper::create_const_column<TYPE_VARCHAR>("x", input_column->size());

    auto result_status = RunLtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

    ASSERT_EQ(result->size(), 5);
    ASSERT_EQ(result_view.value(0).to_string(), "yPQL");
    ASSERT_EQ(result_view.value(1).to_string(), "PQL");
    ASSERT_EQ(result_view.value(2).to_string(), "zxPQL");
    ASSERT_EQ(result_view.value(3).to_string(), " PQL");
    ASSERT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisTrimTest, ltrim_with_null_trim_arg) {
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("xPQL");
    input_column->append_datum("yPQL");
    input_column->append_datum(" PQL");

    ColumnPtr characters = ColumnHelper::create_const_null_column(input_column->size());

    auto result_status = RunLtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

    ASSERT_EQ(result->size(), 3);
    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
    ASSERT_TRUE(result->get(2).is_null());
}

TEST_F(CelonisTrimTest, ltrim_with_trim_args) {
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("Example without leading whitespace");
    input_column->append_datum(" Example with leading whitespace");
    input_column->append_datum("          \n PQL");
    input_column->append_datum("a ");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum(" \n");

    ColumnPtr characters = ColumnHelper::create_const_column<TYPE_VARCHAR>(" \n", input_column->size());

    auto result_status = RunLtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

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
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("000001");
    input_column->append_datum("_____2");
    input_column->append_datum("_ _ 3");
    input_column->append_datum("!$..4");
    input_column->append_datum("_5");
    input_column->append_datum(" 6");

    ColumnPtr characters = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    characters->append_datum("0");
    characters->append_datum("_");
    characters->append_datum(" _");
    characters->append_datum(".!$");
    characters->append_datum("?");
    characters->append_nulls(1);

    auto result_status = RunLtrimNonConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

    ASSERT_EQ(result->size(), 6);
    ASSERT_EQ(result_view.value(0).to_string(), "1");
    ASSERT_EQ(result_view.value(1).to_string(), "2");
    ASSERT_EQ(result_view.value(2).to_string(), "3");
    ASSERT_EQ(result_view.value(3).to_string(), "4");
    ASSERT_EQ(result_view.value(4).to_string(), "_5");
    ASSERT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisTrimTest, ltrim_all_null_constants) {
    ColumnPtr input_column = ColumnHelper::create_const_null_column(10);
    ColumnPtr characters = ColumnHelper::create_const_null_column(10);

    auto result_status = RunLtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    ASSERT_EQ(result->size(), 10);
    ASSERT_TRUE(result->only_null());
}

TEST_F(CelonisTrimTest, rtrim_whitespace_trim_arg) {
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("Example without trailing whitespace");
    input_column->append_datum("Example with trailing whitespace ");
    input_column->append_datum("PQL \n");
    input_column->append_datum(" a");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum("\n ");

    ColumnPtr whitespace = ColumnHelper::create_const_column<TYPE_VARCHAR>(" ", input_column->size());

    auto result_status = RunRtrimConstantCase(input_column, whitespace);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

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
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("Example without trailing whitespace");
    input_column->append_datum("Example with trailing whitespace ");
    input_column->append_datum("PQL \n");
    input_column->append_datum(" a");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum("\n ");

    ColumnPtr whitespace = ColumnHelper::create_const_column<TYPE_VARCHAR>("", input_column->size());

    auto result_status = RunRtrimConstantCase(input_column, whitespace);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

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
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("PQLyx");
    input_column->append_datum("PQLxx");
    input_column->append_datum("PQLxz");
    input_column->append_datum("PQL ");
    input_column->append_nulls(1);

    ColumnPtr characters = ColumnHelper::create_const_column<TYPE_VARCHAR>("x", input_column->size());

    auto result_status = RunRtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

    ASSERT_EQ(result->size(), 5);
    ASSERT_EQ(result_view.value(0).to_string(), "PQLy");
    ASSERT_EQ(result_view.value(1).to_string(), "PQL");
    ASSERT_EQ(result_view.value(2).to_string(), "PQLxz");
    ASSERT_EQ(result_view.value(3).to_string(), "PQL ");
    ASSERT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisTrimTest, rtrim_with_trim_args) {
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("Example without trailing whitespace");
    input_column->append_datum("Example with trailing whitespace ");
    input_column->append_datum("PQL    \n");
    input_column->append_datum(" a");
    input_column->append_datum(" ");
    input_column->append_datum("");
    input_column->append_nulls(1);
    input_column->append_datum("\n ");

    ColumnPtr characters = ColumnHelper::create_const_column<TYPE_VARCHAR>(" \n", input_column->size());

    auto result_status = RunRtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

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
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("xPQL");
    input_column->append_datum("yPQL");
    input_column->append_datum(" PQL");

    ColumnPtr characters = ColumnHelper::create_const_null_column(input_column->size());

    auto result_status = RunRtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

    ASSERT_EQ(result->size(), 3);
    ASSERT_TRUE(result->get(0).is_null());
    ASSERT_TRUE(result->get(1).is_null());
    ASSERT_TRUE(result->get(2).is_null());
}

TEST_F(CelonisTrimTest, rtrim_with_string_column) {
    ColumnPtr input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_datum("100000");
    input_column->append_datum("2_____");
    input_column->append_datum("3 _ _");
    input_column->append_datum("4..$!");
    input_column->append_datum("5_");
    input_column->append_datum("6 ");

    ColumnPtr characters = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    characters->append_datum("0");
    characters->append_datum("_");
    characters->append_datum(" _");
    characters->append_datum(".!$");
    characters->append_datum("?");
    characters->append_nulls(1);

    auto result_status = RunRtrimNonConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();
    const auto result_view = ColumnViewer<TYPE_VARCHAR>(result);

    ASSERT_EQ(result->size(), 6);
    ASSERT_EQ(result_view.value(0).to_string(), "1");
    ASSERT_EQ(result_view.value(1).to_string(), "2");
    ASSERT_EQ(result_view.value(2).to_string(), "3");
    ASSERT_EQ(result_view.value(3).to_string(), "4");
    ASSERT_EQ(result_view.value(4).to_string(), "5_");
    ASSERT_TRUE(result->get(5).is_null());
}

TEST_F(CelonisTrimTest, rtrim_all_null_constants) {
    ColumnPtr input_column = ColumnHelper::create_const_null_column(10);
    ColumnPtr characters = ColumnHelper::create_const_null_column(10);

    auto result_status = RunRtrimConstantCase(input_column, characters);
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    ASSERT_EQ(result->size(), 10);
    ASSERT_TRUE(result->only_null());
}

} // namespace starrocks