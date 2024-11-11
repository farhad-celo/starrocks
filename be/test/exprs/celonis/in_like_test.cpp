#include "exprs/celonis/string_functions.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisInLikeTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        string_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        patterns_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
    }

    void
    AddRow(const Datum& string, const DatumArray& patterns) {
        string_column_->append_datum(string);
        patterns_column_->append_datum(patterns);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisStringFunctions::in_like_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisStringFunctions::in_like_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisStringFunctions::in_like_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisStringFunctions::in_like_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisStringFunctions::in_like(ctx_.get(), {string_column_, patterns_column_});
        return result;
    }

    StatusOr<ColumnPtr>
    RunConstantPatterns(const DatumArray& patterns) {
        patterns_column_->append_datum(patterns);
        const auto nrows = string_column_->size();
        patterns_column_ = ConstColumn::create(patterns_column_, nrows);
        ctx_->set_constant_columns({nullptr, patterns_column_});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr string_column_;
    ColumnPtr patterns_column_;
};

TEST_F(CelonisInLikeTest, const_patterns_normal_cases) {
    {
        Prepare();
        string_column_->append_datum("asddqqW_A_W");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("fdsn_B");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("fdskjd_B_dsa");
        string_column_->append_datum("dsaksdj");
        string_column_->append_datum(kNullDatum);
        const auto result = RunConstantPatterns(DatumArray{"%A%", "%B%"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
        EXPECT_EQ(1L, result->get(4).get_int64());
        EXPECT_EQ(0L, result->get(5).get_int64());
        EXPECT_EQ(0L, result->get(6).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("hallo IN_LIKE");
        string_column_->append_datum("Axyz");
        string_column_->append_datum("vamos a la playa");
        string_column_->append_datum("test test 1,2,3");
        string_column_->append_datum("celosphere");
        string_column_->append_datum("celosphere and celonis");
        string_column_->append_datum("Celonis");
        const auto result = RunConstantPatterns(DatumArray{"test", "xyz", "celonis", "%PQL%", "_"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(0L, result->get(2).get_int64());
        EXPECT_EQ(1L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(1L, result->get(5).get_int64());
        EXPECT_EQ(1L, result->get(6).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("%BCD");
        string_column_->append_datum("_A");
        const auto result = RunConstantPatterns(DatumArray{"\\%b", "\\_a"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("%BCD");
        string_column_->append_datum("_A");
        const auto result = RunConstantPatterns(DatumArray{"\\\\%b", "\\\\_a"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("%BCD");
        string_column_->append_datum("_A");
        string_column_->append_datum("\\%BCD");
        const auto result = RunConstantPatterns(DatumArray{R"(\\\%b)", "\\_a"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("%BCD");
        string_column_->append_datum("_A");
        const auto result = RunConstantPatterns(DatumArray{R"(\\\%b)", R"(\\\_a)"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("a");
        string_column_->append_datum("b");
        const auto result = RunConstantPatterns(DatumArray{"A"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("b%a");
        string_column_->append_datum("b\\");
        string_column_->append_datum("a\\");
        string_column_->append_datum("b");
        const auto result = RunConstantPatterns(DatumArray{"B\\%", "B\\"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(0L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("b_a");
        string_column_->append_datum("b\\");
        string_column_->append_datum("B\\");
        const auto result = RunConstantPatterns(DatumArray{"B\\_", "B\\"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("\\");
        const auto result = RunConstantPatterns(DatumArray{"\\\\"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
    }
}

TEST_F(CelonisInLikeTest, empty_input) {
    Prepare();
    const auto result = RunConstantPatterns(DatumArray{"ä", "ö", "ü", "ß"}).value();
    ASSERT_EQ(string_column_->size(), result->size());
}

TEST_F(CelonisInLikeTest, const_patterns_german_chars) {
    {
        Prepare();
        string_column_->append_datum("Ä");
        string_column_->append_datum("Ö");
        string_column_->append_datum("Ü");
        string_column_->append_datum("ß");
        string_column_->append_datum("A");
        const auto result = RunConstantPatterns(DatumArray{"ä", "ö", "ü", "ß"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(1L, result->get(2).get_int64());
        EXPECT_EQ(1L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("ÄÖÜ");
        string_column_->append_datum("ÖÜ");
        string_column_->append_datum("ÄÜ");
        const auto result = RunConstantPatterns(DatumArray{"äöü", "öü"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(1L, result->get(1).get_int64());
        EXPECT_EQ(0L, result->get(2).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("AÄ");
        string_column_->append_datum("ÄA");
        string_column_->append_datum("ÖO");
        string_column_->append_datum("UÜ");
        const auto result = RunConstantPatterns(DatumArray{"aä", "uü"}).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(1L, result->get(0).get_int64());
        EXPECT_EQ(0L, result->get(1).get_int64());
        EXPECT_EQ(0L, result->get(2).get_int64());
        EXPECT_EQ(1L, result->get(3).get_int64());
    }
}

TEST_F(CelonisInLikeTest, const_patterns_with_null_pattern) {
    Prepare();
    string_column_->append_datum("asddqqW_A_W");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("fdsn_B");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("fdskjd_B_dsa");
    string_column_->append_datum("dsaksdj");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("a");
    const auto result = RunConstantPatterns(DatumArray{"%A%", "%B%", kNullDatum}).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(1L, result->get(4).get_int64());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisInLikeTest, const_patterns_with_duplicate_patterns) {
    Prepare();
    string_column_->append_datum("asddqqW_A_W");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("fdsn_B");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("fdskjd_B_dsa");
    string_column_->append_datum("dsaksdj");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("a");
    const auto result = RunConstantPatterns(DatumArray{"%A%", "%B%", "%A%", kNullDatum, kNullDatum, "%B%"}).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(1L, result->get(4).get_int64());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisInLikeTest, const_null_patterns) {
    Prepare();
    string_column_->append_datum("asddqqW_A_W");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("fdsn_B");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("fdskjd_B_dsa");
    string_column_->append_datum("dsaksdj");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("a");
    const auto result = RunConstantPatterns(DatumArray{kNullDatum}).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisInLikeTest, non_const_patterns) {
    Prepare();
    AddRow("asddqqW_A_W", DatumArray{"%A%", "%B%"});
    AddRow(kNullDatum, DatumArray{"A"});
    AddRow(kNullDatum, DatumArray{"A", kNullDatum});
    AddRow("ÄÖÜ", DatumArray{"äöü", "öü"});
    AddRow("%BCD", DatumArray{R"(\\\%b)"});
    AddRow("\\%BCD", DatumArray{R"(\\\%b)"});
    AddRow("r", DatumArray{"\\r"});
    AddRow("\\", DatumArray{"\\"});
    AddRow("\\\\", DatumArray{"\\\\"});
    AddRow("", DatumArray{""});
    AddRow("b\\", DatumArray{"B\\"});
    AddRow("b", DatumArray{"B\\"});
    AddRow("b%a", DatumArray{"B\\%"});
    AddRow("b_a", DatumArray{"B\\_"});
    AddRow("b\\a", DatumArray{"B\\\\"});
    AddRow("asddqqW_A_W", DatumArray{"%A%", "%B%", "%A%", "%B%"});
    const auto result = Run().value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(0L, result->get(6).get_int64());
    EXPECT_EQ(1L, result->get(7).get_int64());
    EXPECT_EQ(1L, result->get(8).get_int64());
    EXPECT_EQ(1L, result->get(9).get_int64());
    EXPECT_EQ(1L, result->get(10).get_int64());
    EXPECT_EQ(0L, result->get(11).get_int64());
    EXPECT_EQ(1L, result->get(12).get_int64());
    EXPECT_EQ(1L, result->get(13).get_int64());
    EXPECT_EQ(1L, result->get(14).get_int64());
    EXPECT_EQ(1L, result->get(15).get_int64());
}

} // namespace starrocks
