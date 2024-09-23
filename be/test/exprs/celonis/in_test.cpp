#include "exprs/celonis/in.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisInTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    template <LogicalType LT>
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(LT)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        value_column_ = ColumnHelper::create_column(TypeDescriptor(LT), true);
        // Array Literal is not wrapped with ConstColumn.
        // As of 2024-01-30, it has one row in FunctionContext::constant_column_ and it is evaluated and unfolded to
        // multiple rows in /be/src/exprs/array_expr.cpp before it is passed to celonis_in().
        // In this test, we don't unfold the column when we call the function as the function doesn't read it.
        match_array_column_ = ColumnHelper::create_column(celonis::array_type(LT), false);
    }

    void AddRow(const Datum& value, const DatumArray& match_array) {
        value_column_->append_datum(value);
        match_array_column_->append_datum(match_array);
    }

    template <LogicalType LT>
    StatusOr<ColumnPtr> Run() {
        RETURN_IF_ERROR(CelonisIn<LT>::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        RETURN_IF_ERROR(CelonisIn<LT>::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        auto result = CelonisIn<LT>::in(ctx_.get(), {value_column_, match_array_column_});
        RETURN_IF_ERROR(CelonisIn<LT>::close(ctx_.get(), FunctionContext::THREAD_LOCAL));
        RETURN_IF_ERROR(CelonisIn<LT>::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        return result;
    }

    template <LogicalType LT>
    StatusOr<ColumnPtr> RunConstantMatch(DatumArray match_array) {
        match_array_column_->append_datum(match_array);
        const auto nrows = value_column_->size();
        ctx_->set_constant_columns({nullptr, ConstColumn::create(match_array_column_, nrows)});
        return Run<LT>();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr value_column_;
    ColumnPtr match_array_column_;
};

TEST_F(CelonisInTest, celonis_in_string_data_no_null_in_match_list) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(Datum());
    value_column_->append_datum("string3");

    auto match_array = DatumArray{"string1", "string3"};

    const auto result = RunConstantMatch<LT>(match_array).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_string_data_null_in_match_list) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(Datum());
    value_column_->append_datum("string3");

    auto match_array = DatumArray{"string1", "string3", Datum()};

    const auto result = RunConstantMatch<LT>(match_array).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_string_empty_match_list) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(Datum());
    value_column_->append_datum("string3");

    auto match_array = DatumArray{};

    const auto result = RunConstantMatch<LT>(match_array).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_int) {
    const LogicalType LT = TYPE_INT;
    Prepare<LT>();

    value_column_->append_datum(1);
    value_column_->append_datum(2);
    value_column_->append_datum(Datum());
    value_column_->append_datum(3);

    auto match_array = DatumArray{Datum{}, 2};

    const auto result = RunConstantMatch<LT>(match_array).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(true, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_bigint) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();

    value_column_->append_datum(1L);
    value_column_->append_datum(2L);
    value_column_->append_datum(Datum());
    value_column_->append_datum(INT64_MIN);
    value_column_->append_datum(INT64_MAX);

    auto match_array = DatumArray{1L, INT64_MIN};

    const auto result = RunConstantMatch<LT>(match_array).value();
    EXPECT_EQ(5, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
    EXPECT_EQ(false, result->get(4).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_double) {
    const LogicalType LT = TYPE_DOUBLE;
    Prepare<LT>();

    value_column_->append_datum(1.1);
    value_column_->append_datum(2.2);
    value_column_->append_datum(Datum());
    value_column_->append_datum(3.3);

    auto match_array = DatumArray{1.1, 3.3};

    const auto result = RunConstantMatch<LT>(match_array).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_datetime) {
    const LogicalType LT = TYPE_DATETIME;
    Prepare<LT>();

    auto datetime1 = TimestampValue::create(2017, 10, 1, 2, 32, 32);
    auto datetime2 = TimestampValue::create(2017, 10, 2, 2, 32, 32);
    auto datetime3 = TimestampValue::create(2017, 10, 3, 2, 32, 32);
    auto datetime4 = TimestampValue::create(2017, 10, 4, 2, 32, 32);

    value_column_->append_datum(datetime1);
    value_column_->append_datum(datetime3);
    value_column_->append_datum(Datum());
    value_column_->append_datum(datetime4);

    auto match_array = DatumArray{datetime1, datetime2, datetime3, Datum{}};

    const auto result = RunConstantMatch<LT>(match_array).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(true, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_non_constant_string) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    AddRow("s1", {"s1", "s2", "s3"});
    AddRow("s1", {Datum{}, "s2", "s3"});
    AddRow(Datum{}, {"s1", "s2", "s3"});
    AddRow(Datum{}, {"s1", "s2", "s3", Datum{}});

    const auto result = Run<LT>().value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_non_constant_int) {
    const LogicalType LT = TYPE_INT;
    Prepare<LT>();

    AddRow(1, {10, 20, 30});
    AddRow(100, {100, 200, 300, Datum{}});
    AddRow(Datum{}, {10, 20, Datum{}, 30});
    AddRow(Datum{}, {1, 2, 3});
    AddRow(Datum{}, {Datum{}});

    const auto result = Run<LT>().value();
    EXPECT_EQ(5, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(true, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
    EXPECT_EQ(true, result->get(4).get_uint8());
}

} // namespace starrocks
