#include "exprs/celonis/string_functions.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisMatchStringsTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        string_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        match_strings_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        top_k_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);
        separator_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    }

    void AddRow(const Datum& value, const Datum& match_array, const Datum& top_k, const Datum& separator) {
        string_column_->append_datum(value);
        match_strings_column_->append_datum(match_array);
        top_k_column_->append_datum(top_k);
        separator_column_->append_datum(separator);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisStringFunctions::match_strings_close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisStringFunctions::match_strings_prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisStringFunctions::match_strings_close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisStringFunctions::match_strings_prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        auto result = CelonisStringFunctions::match_strings(ctx_.get(),
                                                            {string_column_, match_strings_column_, top_k_column_,
                                                             separator_column_});
        return result;
    }

    StatusOr<ColumnPtr> RunConstantMatch(const Datum& match_array, const Datum& top_k, const Datum& separator) {
        match_strings_column_->append_datum(match_array);
        top_k_column_->append_datum(top_k);
        separator_column_->append_datum(separator);
        const auto nrows = string_column_->size();
        match_strings_column_ = ConstColumn::create(match_strings_column_, nrows);
        top_k_column_ = ConstColumn::create(top_k_column_, nrows);
        separator_column_ = ConstColumn::create(separator_column_, nrows);
        ctx_->set_constant_columns({nullptr, match_strings_column_, top_k_column_, separator_column_});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr string_column_;
    ColumnPtr match_strings_column_;
    ColumnPtr top_k_column_;
    ColumnPtr separator_column_;

};

TEST_F(CelonisMatchStringsTest, empty_input) {
    {
        Prepare();
        const auto result = Run().value();
        EXPECT_EQ(0, result->size());
    }
    {
        Prepare();
        const auto result = RunConstantMatch(DatumArray{"match"}, 5, ";").value();
        EXPECT_EQ(0, result->size());
    }
}

TEST_F(CelonisMatchStringsTest, const_null_match_strings) {
    Prepare();
    string_column_->append_datum("Shirts");
    string_column_->append_datum("Pants");
    const auto result = RunConstantMatch(kNullDatum, kNullDatum, kNullDatum).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisMatchStringsTest, const_match_strings_normal_case_1) {
    Prepare();
    string_column_->append_datum("Shirts");
    string_column_->append_datum("Pants");
    const auto result = RunConstantMatch(DatumArray{"T-Shirt", "Sweatshirt", "Short pants", "Sweatpants"}, kNullDatum,
                                         kNullDatum).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("T-Shirt", result->get(0).get_slice());
    EXPECT_EQ("Sweatpants", result->get(1).get_slice());
}

TEST_F(CelonisMatchStringsTest, const_match_strings_normal_case_2) {
    Prepare();
    string_column_->append_datum("Shirt");
    string_column_->append_datum("Pants");
    const auto result = RunConstantMatch(DatumArray{"T-Shirt", "Sweatshirt", "Short pants", "Sweatpants"}, 2,
                                         "##").value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("T-Shirt##Sweatshirt", result->get(0).get_slice());
    EXPECT_EQ("Sweatpants##Short pants", result->get(1).get_slice());
}

TEST_F(CelonisMatchStringsTest, const_match_strings_normal_case_3) {
    Prepare();
    string_column_->append_datum("xyz");
    string_column_->append_datum("T-Shirt");
    string_column_->append_datum("abc");
    const auto result = RunConstantMatch(DatumArray{"Shirt", "Sweatshirt"}, 2, kNullDatum).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("", result->get(0).get_slice());
    EXPECT_EQ("Shirt, Sweatshirt", result->get(1).get_slice());
    EXPECT_EQ("Sweatshirt", result->get(2).get_slice());
}

TEST_F(CelonisMatchStringsTest, const_match_strings_normal_case_4) {
    Prepare();
    string_column_->append_datum("Shirt");
    string_column_->append_datum("Pants");
    const auto result = RunConstantMatch(DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants"}, 2,
                                         kNullDatum).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("T-Shirt, Sweatpants", result->get(0).get_slice());
    EXPECT_EQ("Sweatpants, T-Shirt", result->get(1).get_slice());
}

TEST_F(CelonisMatchStringsTest, const_match_strings_normal_case_5) {
    Prepare();
    string_column_->append_datum("Shirt");
    string_column_->append_datum("Pants");
    const auto result = RunConstantMatch(DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants"}, 10,
                                         kNullDatum).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("T-Shirt, Sweatpants", result->get(0).get_slice());
    EXPECT_EQ("Sweatpants, T-Shirt", result->get(1).get_slice());
}

TEST_F(CelonisMatchStringsTest, const_match_strings_normal_case_6) {
    Prepare();
    string_column_->append_datum("Shirt");
    string_column_->append_datum("Pants");
    string_column_->append_datum("Pants");
    string_column_->append_datum("Shirt");
    const auto result = RunConstantMatch(DatumArray{"BSP", "CSP", "DSP", "ASP"}, 10,
                                         kNullDatum).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("ASP, BSP, CSP, DSP", result->get(0).get_slice());
    EXPECT_EQ("ASP, BSP, CSP, DSP", result->get(1).get_slice());
    EXPECT_EQ("ASP, BSP, CSP, DSP", result->get(2).get_slice());
    EXPECT_EQ("ASP, BSP, CSP, DSP", result->get(3).get_slice());
}

TEST_F(CelonisMatchStringsTest, null_input_string_and_const_match_strings) {
    Prepare();
    string_column_->append_datum("Shirt");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("Shirt");
    const auto result = RunConstantMatch(DatumArray{"T-Shirt", "Sweatshirt", "Short pants", "Sweatpants"}, 2,
                                         "##").value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("T-Shirt##Sweatshirt", result->get(0).get_slice());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ("T-Shirt##Sweatshirt", result->get(2).get_slice());
}

TEST_F(CelonisMatchStringsTest, null_input_string_and_null_const_match_strings) {
    Prepare();
    string_column_->append_datum("Shirt");
    string_column_->append_datum(kNullDatum);
    const auto result = RunConstantMatch(kNullDatum, 2, "##").value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisMatchStringsTest, const_match_strings_zero_top_k) {
    Prepare();
    string_column_->append_datum("Shirt");
    const auto result = RunConstantMatch(DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants"}, 0, "##");
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "CELONIS_MATCH_STRINGS: top_k must be positive.");
}

TEST_F(CelonisMatchStringsTest, const_match_strings_negative_top_k) {
    Prepare();
    string_column_->append_datum("Shirt");
    const auto result = RunConstantMatch(DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants"}, -1, "##");
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "CELONIS_MATCH_STRINGS: top_k must be positive.");
}

TEST_F(CelonisMatchStringsTest, null_input_string_and_non_const_match_strings) {
    Prepare();
    AddRow("Shirt", DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants", kNullDatum}, 2, "#");
    AddRow(kNullDatum, DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants", kNullDatum}, 2, "%");
    const auto result = Run().value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("T-Shirt#Sweatpants", result->get(0).get_slice());
    EXPECT_TRUE(result->get(1).is_null());
}

TEST_F(CelonisMatchStringsTest, non_const_match_strings_normal_case) {
    Prepare();
    AddRow("Shirt", DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants", kNullDatum}, 2, "#");
    AddRow("", DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants", kNullDatum}, 2, "%");
    AddRow("Shirt", DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants", kNullDatum}, 10, ";");
    const auto result = Run().value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ("T-Shirt#Sweatpants", result->get(0).get_slice());
    EXPECT_EQ("", result->get(1).get_slice());
    EXPECT_EQ("T-Shirt;Sweatpants", result->get(2).get_slice());
}

TEST_F(CelonisMatchStringsTest, non_const_match_strings_zero_top_k) {
    Prepare();
    AddRow("Shirt", DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants", kNullDatum}, 0, "#");
    const auto result = Run();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "CELONIS_MATCH_STRINGS: top_k must be positive.");
}

TEST_F(CelonisMatchStringsTest, non_const_match_strings_negative_top_k) {
    Prepare();
    AddRow("Shirt", DatumArray{"T-Shirt", "T-Shirt", "Sweatpants", "Sweatpants", kNullDatum}, -2, "#");
    const auto result = Run();
    ASSERT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ(result.status().message(), "CELONIS_MATCH_STRINGS: top_k must be positive.");
}

} // namespace starrocks
