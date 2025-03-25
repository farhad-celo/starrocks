#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"
#include "util.h"

namespace starrocks {

std::string int128_to_string(int128_t num) {
    if (num == 0) return "0";
    std::string result;
    bool is_negative = (num < 0);

    if (is_negative) {
        num = -num;
    }

    while (num != 0) {
        int64_t digit = num % 10;
        result = static_cast<char>('0' + digit) + result;
        num /= 10;
    }
    if (is_negative) {
        result = '-' + result;
    }
    return result;
}

class CelonisStringFunctionsTest : public testing::Test {

    TypeDescriptor TYPE_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);

protected:
    void translate(Columns columns, const std::vector<std::string>& res) {
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
        auto context = ctx.get();
        context->set_constant_columns(columns);

        ASSERT_TRUE(
                CelonisStringFunctions::translate_prepare(context,
                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

        const auto result = CelonisStringFunctions::translate(context, columns).value();
        const auto v = ColumnHelper::as_column<BinaryColumn>(result);

        for (int i = 0; i < res.size(); ++i) {
            EXPECT_EQ(res[i], v->get_data()[i].to_string());
        }

        ASSERT_TRUE(
                CelonisStringFunctions::translate_close(context,
                                                        FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                        .ok());
    }
};

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v3_collision) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    Columns columns;
    auto column1 = BinaryColumn::create();
    column1->append("70B5E8DAF8BF1EEE91CC8D383DB5A16E");
    column1->append("78AC441C9BB21EDEB483CA3CB88FDC57");

    auto column2 = BinaryColumn::create();
    column2->append("010");
    column2->append("010");

    columns.emplace_back(column1);
    columns.emplace_back(column2);

    ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();

    ASSERT_EQ(2, result->size());
    EXPECT_NE(int128_to_string(result->get(0).get_int128()), int128_to_string(result->get(1).get_int128()));
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v3_concat_collision) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    Columns columns;
    auto column1 = BinaryColumn::create();
    column1->append("22");
    column1->append("2");
    column1->append("1");
    column1->append("11111111111");

    auto column2 = BinaryColumn::create();
    column2->append("44");
    column2->append("244");
    column2->append("1111111111");
    column2->append("");

    columns.emplace_back(column1);
    columns.emplace_back(column2);

    ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();

    ASSERT_EQ(4, result->size());
    EXPECT_NE(int128_to_string(result->get(0).get_int128()), int128_to_string(result->get(1).get_int128()));
    EXPECT_NE(int128_to_string(result->get(2).get_int128()), int128_to_string(result->get(3).get_int128()));
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v3_array_input) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(0, result->size());
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello"});
        column->append_datum(DatumArray{"starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-39860275131497362562110746998887222515", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"hello"});
        column->append_datum(DatumArray{"starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-39860275131497362562110746998887222515", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("144019643735392052535165383538110996661", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-133694396197743686302797954021954852834", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("144019643735392052535165383538110996661", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-133694396197743686302797954021954852834", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello", kNullDatum, "world"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("18679293785523661598860211660857963851", int128_to_string(result->get(0).get_int128()));
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);
        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("39062573314113587086746376608795509816", int128_to_string(result->get(0).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"Celonis"});
        column->append_datum(kNullDatum);
        column->append_datum(DatumArray{kNullDatum});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(3, result->size());
        EXPECT_EQ("62891042711506979871088750514650369910", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("39062573314113587086746376608795509816", int128_to_string(result->get(1).get_int128()));
        EXPECT_EQ("-94392335423945087845847157094108824607", int128_to_string(result->get(2).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"Celonis", kNullDatum});
        column->append_datum(DatumArray{kNullDatum, "Celonis"});
        column->append_datum(DatumArray{kNullDatum});
        column->append_datum(DatumArray{kNullDatum, kNullDatum});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column}).value();
        ASSERT_EQ(4, result->size());
        EXPECT_NE(int128_to_string(result->get(0).get_int128()), int128_to_string(result->get(1).get_int128()));
        EXPECT_NE(int128_to_string(result->get(1).get_int128()), int128_to_string(result->get(2).get_int128()));
        EXPECT_NE(int128_to_string(result->get(2).get_int128()), int128_to_string(result->get(3).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{XXHASH3_128_NULL_STRING.c_str()});
        const auto result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128_V3: string value conflicts with the reserved string '_$CeL0nIs_ReSeRvEd_NuLl_'.");
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{XXHASH3_128_NULL_ARRAY_STRING.c_str()});
        const auto result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {column});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128_V3: string value conflicts with the reserved string '_$CeL0nIs_ReSeRvEd_NuLl_aRrAy_'.");
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v3) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        Columns columns;
        auto column = BinaryColumn::create();
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();
        ASSERT_EQ(0, result->size());
    }
    {
        Columns columns;
        auto column = BinaryColumn::create();
        column->append("hello");
        column->append("starrocks");
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-39860275131497362562110746998887222515", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        column1->append("hello");
        auto column2 = BinaryColumn::create();
        column2->append("world");
        column2->append("starrocks");
        columns.emplace_back(column1);
        columns.emplace_back(column2);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();

        ASSERT_EQ(2, result->size());
        EXPECT_EQ("144019643735392052535165383538110996661", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-133694396197743686302797954021954852834", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        auto column2 = ColumnHelper::create_const_null_column(1);
        auto column3 = BinaryColumn::create();
        column3->append("world");
        columns.emplace_back(column1);
        columns.emplace_back(column2);
        columns.emplace_back(column3);

        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("18679293785523661598860211660857963851", int128_to_string(result->get(0).get_int128()));
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);
        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("-94392335423945087845847157094108824607", int128_to_string(result->get(0).get_int128()));
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("Celonis");
        strings->append_datum(kNullDatum);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {strings}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("62891042711506979871088750514650369910", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-94392335423945087845847157094108824607", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum(XXHASH3_128_NULL_STRING.c_str());
        strings->append_datum(kNullDatum);
        const auto result = CelonisStringFunctions::xx_hash3_128_v3(ctx.get(), {strings});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128_V3: string value conflicts with the reserved string '_$CeL0nIs_ReSeRvEd_NuLl_'.");
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_collision) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    Columns columns;
    auto column1 = BinaryColumn::create();
    column1->append("70B5E8DAF8BF1EEE91CC8D383DB5A16E");
    column1->append("78AC441C9BB21EDEB483CA3CB88FDC57");

    auto column2 = BinaryColumn::create();
    column2->append("010");
    column2->append("010");

    columns.emplace_back(column1);
    columns.emplace_back(column2);

    ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), columns).value();

    ASSERT_EQ(2, result->size());
    EXPECT_EQ("-145184912315577085865191747645648721411", int128_to_string(result->get(0).get_int128()));
    EXPECT_EQ("-145184912315577085865191747645648721411", int128_to_string(result->get(1).get_int128()));
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_array_input) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(0, result->size());
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello"});
        column->append_datum(DatumArray{"starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("70964585907158640341122805077717094742", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"hello"});
        column->append_datum(DatumArray{"starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("70964585907158640341122805077717094742", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-9508340982777299797928774324431085410", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-60119840840360818224922178158465423753", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-9508340982777299797928774324431085410", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-60119840840360818224922178158465423753", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello", kNullDatum, "world"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("74246737348891246928363368458797820763", int128_to_string(result->get(0).get_int128()));
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);
        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("-55107912451276212254785155889373354613", int128_to_string(result->get(0).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"Celonis"});
        column->append_datum(kNullDatum);
        column->append_datum(DatumArray{kNullDatum});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(3, result->size());
        EXPECT_EQ("113354056479506190712662670385450615649", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-55107912451276212254785155889373354613", int128_to_string(result->get(1).get_int128()));
        EXPECT_EQ("140510453822038601413216693103982955033", int128_to_string(result->get(2).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"Celonis", kNullDatum});
        column->append_datum(DatumArray{kNullDatum, "Celonis"});
        column->append_datum(DatumArray{kNullDatum});
        column->append_datum(DatumArray{kNullDatum, kNullDatum});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column}).value();
        ASSERT_EQ(4, result->size());
        EXPECT_NE(int128_to_string(result->get(0).get_int128()), int128_to_string(result->get(1).get_int128()));
        EXPECT_NE(int128_to_string(result->get(1).get_int128()), int128_to_string(result->get(2).get_int128()));
        EXPECT_NE(int128_to_string(result->get(2).get_int128()), int128_to_string(result->get(3).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{XXHASH3_128_NULL_STRING.c_str()});
        const auto result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128: string value conflicts with the reserved NULL string '_$CeL0nIs_ReSeRvEd_NuLl_'.");
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{XXHASH3_128_NULL_ARRAY_STRING.c_str()});
        const auto result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {column});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128: string value conflicts with the reserved NULL array string '_$CeL0nIs_ReSeRvEd_NuLl_aRrAy_'.");
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        Columns columns;
        auto column = BinaryColumn::create();
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), columns).value();
        ASSERT_EQ(0, result->size());
    }
    {
        Columns columns;
        auto column = BinaryColumn::create();
        column->append("hello");
        column->append("starrocks");
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), columns).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("70964585907158640341122805077717094742", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        column1->append("hello");
        auto column2 = BinaryColumn::create();
        column2->append("world");
        column2->append("starrocks");
        columns.emplace_back(column1);
        columns.emplace_back(column2);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), columns).value();

        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-9508340982777299797928774324431085410", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-60119840840360818224922178158465423753", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        auto column2 = ColumnHelper::create_const_null_column(1);
        auto column3 = BinaryColumn::create();
        column3->append("world");
        columns.emplace_back(column1);
        columns.emplace_back(column2);
        columns.emplace_back(column3);

        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("74246737348891246928363368458797820763", int128_to_string(result->get(0).get_int128()));
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);
        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("140510453822038601413216693103982955033", int128_to_string(result->get(0).get_int128()));
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("Celonis");
        strings->append_datum(kNullDatum);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {strings}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("113354056479506190712662670385450615649", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("140510453822038601413216693103982955033", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum(XXHASH3_128_NULL_STRING.c_str());
        strings->append_datum(kNullDatum);
        const auto result = CelonisStringFunctions::xx_hash3_128(ctx.get(), {strings});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128: string value conflicts with the reserved NULL string '_$CeL0nIs_ReSeRvEd_NuLl_'.");
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v2_const_array_input) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello"});
        column = ConstColumn::create(column, 3);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(3, result->size());
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(1).get_int128()));
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(2).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(kNullDatum);
        column = ConstColumn::create(column, 3);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(3, result->size());
        EXPECT_EQ("-55107912451276212254785155889373354613", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-55107912451276212254785155889373354613", int128_to_string(result->get(1).get_int128()));
        EXPECT_EQ("-55107912451276212254785155889373354613", int128_to_string(result->get(2).get_int128()));
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v2_array_input) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(0, result->size());
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello"});
        column->append_datum(DatumArray{"starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("70964585907158640341122805077717094742", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"hello"});
        column->append_datum(DatumArray{"starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("70964585907158640341122805077717094742", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-45235302294609689438180196264627189905", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("28764148956857839822332210107202390864", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-45235302294609689438180196264627189905", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("28764148956857839822332210107202390864", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        // hash value is different from {"hello", "world"}
        column->append_datum(DatumArray{"hello", kNullDatum, "world"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("-134640440316097477983161488793623285574", int128_to_string(result->get(0).get_int128()));
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);
        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("-55107912451276212254785155889373354613", int128_to_string(result->get(0).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"Celonis"});
        column->append_datum(kNullDatum);
        column->append_datum(DatumArray{kNullDatum});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(3, result->size());
        EXPECT_EQ("113354056479506190712662670385450615649", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-55107912451276212254785155889373354613", int128_to_string(result->get(1).get_int128()));
        EXPECT_EQ("140510453822038601413216693103982955033", int128_to_string(result->get(2).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"Celonis", kNullDatum});
        column->append_datum(DatumArray{kNullDatum, "Celonis"});
        column->append_datum(DatumArray{kNullDatum});
        column->append_datum(DatumArray{kNullDatum, kNullDatum});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column}).value();
        ASSERT_EQ(4, result->size());
        EXPECT_NE(int128_to_string(result->get(0).get_int128()), int128_to_string(result->get(1).get_int128()));
        EXPECT_NE(int128_to_string(result->get(1).get_int128()), int128_to_string(result->get(2).get_int128()));
        EXPECT_NE(int128_to_string(result->get(2).get_int128()), int128_to_string(result->get(3).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{XXHASH3_128_NULL_STRING.c_str()});
        const auto result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128_V2: string value conflicts with the reserved string '_$CeL0nIs_ReSeRvEd_NuLl_'.");
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{XXHASH3_128_NULL_ARRAY_STRING.c_str()});
        const auto result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {column});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128_V2: string value conflicts with the reserved string '_$CeL0nIs_ReSeRvEd_NuLl_aRrAy_'.");
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v2_collision) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("70B5E8DAF8BF1EEE91CC8D383DB5A16E");
        column1->append("78AC441C9BB21EDEB483CA3CB88FDC57");

        auto column2 = BinaryColumn::create();
        column2->append("010");
        column2->append("010");

        columns.emplace_back(column1);
        columns.emplace_back(column2);

        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();

        ASSERT_EQ(2, result->size());
        EXPECT_EQ("38874550928544707556055577710229448382", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("75566834076540376762161358412094155014", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column = BinaryColumn::create();
        column->append("70B5E8DAF8BF1EEE91CC8D383DB5A16E");
        column->append("78AC441C9BB21EDEB483CA3CB88FDC57");
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-73790161438626422886160846512159552713", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-59154098759970055452595756044874511561", int128_to_string(result->get(1).get_int128()));
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_v2) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        Columns columns;
        auto column = BinaryColumn::create();
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();
        ASSERT_EQ(0, result->size());
    }
    {
        Columns columns;
        auto column = BinaryColumn::create();
        column->append("hello");
        column->append("starrocks");
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-98478366302105124680504504609445627880", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("70964585907158640341122805077717094742", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        column1->append("hello");

        auto column2 = BinaryColumn::create();
        column2->append("world");
        column2->append("starrocks");

        columns.emplace_back(column1);
        columns.emplace_back(column2);

        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();

        ASSERT_EQ(2, result->size());
        EXPECT_EQ("-45235302294609689438180196264627189905", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("28764148956857839822332210107202390864", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        auto column2 = ColumnHelper::create_const_null_column(1);
        auto column3 = BinaryColumn::create();
        column3->append("world");

        columns.emplace_back(column1);
        columns.emplace_back(column2);
        columns.emplace_back(column3);

        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("-134640440316097477983161488793623285574", int128_to_string(result->get(0).get_int128()));
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);

        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_EQ("140510453822038601413216693103982955033", int128_to_string(result->get(0).get_int128()));
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("Celonis");
        strings->append_datum(kNullDatum);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {strings}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("113354056479506190712662670385450615649", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("140510453822038601413216693103982955033", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum(XXHASH3_128_NULL_STRING.c_str());
        strings->append_datum(kNullDatum);
        const auto result = CelonisStringFunctions::xx_hash3_128_v2(ctx.get(), {strings});
        EXPECT_EQ(result.status().message(),
                  "CELONIS_XX_HASH3_128_V2: string value conflicts with the reserved string '_$CeL0nIs_ReSeRvEd_NuLl_'.");
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_nullable_collision) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    Columns columns;
    auto column1 = BinaryColumn::create();
    column1->append("70B5E8DAF8BF1EEE91CC8D383DB5A16E");
    column1->append("78AC441C9BB21EDEB483CA3CB88FDC57");

    auto column2 = BinaryColumn::create();
    column2->append("010");
    column2->append("010");

    columns.emplace_back(column1);
    columns.emplace_back(column2);

    ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();

    ASSERT_EQ(2, result->size());
    EXPECT_NE(int128_to_string(result->get(0).get_int128()), int128_to_string(result->get(1).get_int128()));
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_nullable_concat_collision) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    Columns columns;
    auto column1 = BinaryColumn::create();
    column1->append("22");
    column1->append("2");
    column1->append("1");
    column1->append("11111111111");

    auto column2 = BinaryColumn::create();
    column2->append("44");
    column2->append("244");
    column2->append("1111111111");
    column2->append("");

    columns.emplace_back(column1);
    columns.emplace_back(column2);

    ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();

    ASSERT_EQ(4, result->size());
    EXPECT_NE(int128_to_string(result->get(0).get_int128()), int128_to_string(result->get(1).get_int128()));
    EXPECT_NE(int128_to_string(result->get(2).get_int128()), int128_to_string(result->get(3).get_int128()));
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_nullable_const_array_input) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello"});
        column = ConstColumn::create(column, 3);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(column->size(), result->size());
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(1).get_int128()));
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(2).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(kNullDatum);
        column = ConstColumn::create(column, 3);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(column->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_TRUE(result->get(2).is_null());
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_nullable_array_input) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(0, result->size());
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello"});
        column->append_datum(DatumArray{"starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-39860275131497362562110746998887222515", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("144019643735392052535165383538110996661", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-133694396197743686302797954021954852834", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        column->append_datum(DatumArray{"hello", "world"});
        column->append_datum(DatumArray{"hello", "starrocks"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("144019643735392052535165383538110996661", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-133694396197743686302797954021954852834", int128_to_string(result->get(1).get_int128()));
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(DatumArray{"hello", kNullDatum, "world"});
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        column->append_datum(kNullDatum);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {column}).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);
        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto strings = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        strings->append_datum(DatumArray{"Celonis"});
        strings->append_datum(kNullDatum);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {strings}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("62891042711506979871088750514650369910", int128_to_string(result->get(0).get_int128()));
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisStringFunctionsTest, test_xx_hash3_128_nullable) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_LARGEINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    {
        Columns columns;
        auto column = BinaryColumn::create();
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();
        ASSERT_EQ(0, result->size());
    }
    {
        Columns columns;
        auto column = BinaryColumn::create();
        column->append("hello");
        column->append("starrocks");
        columns.emplace_back(column);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("38559703224030507026617373843003362347", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-39860275131497362562110746998887222515", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        column1->append("hello");

        auto column2 = BinaryColumn::create();
        column2->append("world");
        column2->append("starrocks");

        columns.emplace_back(column1);
        columns.emplace_back(column2);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();

        ASSERT_EQ(2, result->size());
        EXPECT_EQ("144019643735392052535165383538110996661", int128_to_string(result->get(0).get_int128()));
        EXPECT_EQ("-133694396197743686302797954021954852834", int128_to_string(result->get(1).get_int128()));
    }
    {
        Columns columns;
        auto column1 = BinaryColumn::create();
        column1->append("hello");
        auto column2 = ColumnHelper::create_const_null_column(1);
        auto column3 = BinaryColumn::create();
        column3->append("world");

        columns.emplace_back(column1);
        columns.emplace_back(column2);
        columns.emplace_back(column3);

        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Columns columns;
        auto column1 = ColumnHelper::create_const_null_column(1);

        columns.emplace_back(column1);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), columns).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("Celonis");
        strings->append_datum(kNullDatum);
        ColumnPtr result = CelonisStringFunctions::xx_hash3_128_nullable(ctx.get(), {strings}).value();
        ASSERT_EQ(2, result->size());
        EXPECT_EQ("62891042711506979871088750514650369910", int128_to_string(result->get(0).get_int128()));
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisStringFunctionsTest, translate_null_input) {
    Columns columns;

    auto str = BinaryColumn::create();
    str->append("dummy");
    str->append("007");

    auto nulls = NullColumn::create();
    nulls->append(1);
    nulls->append(0);

    columns.emplace_back(NullableColumn::create(str, nulls));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("0", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("a", 1));

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto context = ctx.get();
    context->set_constant_columns(columns);

    ASSERT_TRUE(
            CelonisStringFunctions::translate_prepare(context,
                                                      FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

    const auto result = CelonisStringFunctions::translate(context, columns).value();
    const auto v = ColumnHelper::as_column<NullableColumn>(result);
    ASSERT_TRUE(v->has_null());
    ASSERT_EQ(2, v->size());
    ASSERT_TRUE(v->is_null(0));

    ASSERT_FALSE(v->is_null(1));
    EXPECT_EQ("aa7", v->get(1).get_slice());

    ASSERT_TRUE(
            CelonisStringFunctions::translate_close(context,
                                                    FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                    .ok());
}

TEST_F(CelonisStringFunctionsTest, translate_single_char) {
    Columns columns;

    auto str = BinaryColumn::create();
    const std::string strs[] = {"0", "ä00ä0z"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("0", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("a", 1));

    translate(columns, {"a", "äaaäaz"});
}

TEST_F(CelonisStringFunctionsTest, translate_single_char_utf8) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {"ä", "Aa Zz äÄä Öö Üü", "ÄääÄ öÖ üÜ"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("ä", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("Ä", 1));

    translate(columns, {"Ä", "Aa Zz ÄÄÄ Öö Üü", "ÄÄÄÄ öÖ üÜ"});
}

TEST_F(CelonisStringFunctionsTest, translate_multi_char_symbol) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {".,", ",.", "33.333,33"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(".,", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(",.", 1));

    translate(columns, {",.", ".,", "33,333.33"});
}

TEST_F(CelonisStringFunctionsTest, translate_multi_char_char_symbol_combination) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {"F-", "-F", "FOO-BAR"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("F-", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("Z+", 1));

    translate(columns, {"Z+", "+Z", "ZOO+BAR"});
}

TEST_F(CelonisStringFunctionsTest, translate_multi_char_symbol_char_combination) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {"F-", "-F", "FOO-BAR"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("-F", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("+Z", 1));

    translate(columns, {"Z+", "+Z", "ZOO+BAR"});
}

TEST_F(CelonisStringFunctionsTest, translate_empty_pattern) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {"0123456789", "9876 543210", "abc", "0ÄäA 0ÖöO 0ÜüU ZZ", ""};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("", 1));

    translate(columns, {"0123456789", "9876 543210", "abc", "0ÄäA 0ÖöO 0ÜüU ZZ", ""});
}

TEST_F(CelonisStringFunctionsTest, translate_multi_char_digit_2_char) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {"0123456789", "9876543210", "00.000,00", "11.111,11", "99.999,99"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("0123456789", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("ABCDEFGHIJ", 1));

    translate(columns, {"ABCDEFGHIJ", "JIHGFEDCBA", "AA.AAA,AA", "BB.BBB,BB", "JJ.JJJ,JJ"});
}

TEST_F(CelonisStringFunctionsTest, translate_lower_utf8) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {"AOUZÄÖÜ", "Ü Ö Ä A O U Z", "0ÄäA 0ÖöO 0ÜüU ZZ"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("AOUZÄÖÜ", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("aouzäöü", 1));

    translate(columns, {"aouzäöü", "ü ö ä a o u z", "0ääa 0ööo 0üüu zz"});
}

TEST_F(CelonisStringFunctionsTest, translate_upper_utf8) {
    Columns columns;

    auto str = BinaryColumn::create();
    std::string strs[] = {"aouzäöü", "ü ö ä a o u z", "0äÄa 0öÖo 0üÜu zz"};
    for (int i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i) {
        str->append(strs[i]);
    }
    columns.emplace_back(str);
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("aouzäöü", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("AOUZÄÖÜ", 1));

    translate(columns, {"AOUZÄÖÜ", "Ü Ö Ä A O U Z", "0ÄÄA 0ÖÖO 0ÜÜU ZZ"});
}

TEST(CelonisStringFunctionsSanitizeStringTest, Simple) {
    constexpr const char* VALID_STR{"ßäöü asdfinasodf2 ifu 8we9fdfn k298e7"};

    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

    input->append_nulls(1);
    input->append_datum(VALID_STR);
    input->append_datum("\xFF");
    input->append_datum("\xC1\xBF");  // 11000001 10111111 must be encoded as ASCII
    input->append_datum("This is \xC3\xE4 invalid");

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    const auto result = CelonisStringFunctions::sanitize_invalid_utf8(ctx.get(), {input}).value();

    ASSERT_EQ(input->size(), result->size());
    const auto v = ColumnHelper::as_column<NullableColumn>(result);
    ASSERT_TRUE(v->has_null());
    ASSERT_TRUE(v->is_null(0));

    EXPECT_EQ(v->get(1).get_slice(), VALID_STR);
    EXPECT_EQ(v->get(2).get_slice(), "?");
    EXPECT_EQ(v->get(3).get_slice(), "??");
    EXPECT_EQ(v->get(4).get_slice(), "This is ? invalid");
}

TEST(CelonisStringFunctionsSanitizeStringTest, NullTerminated) {
    using namespace std::string_literals;

    std::vector<std::string> input_strs{
            "\0"s,
            "abc\0def"s,
            "\0abc\0def"s,
            "Invalid \xFF and \0 valid str"s
    };

    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    for (const auto& str: input_strs) {
        input->append_datum(Slice(str));
    }

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    const auto result = CelonisStringFunctions::sanitize_invalid_utf8(ctx.get(), {input}).value();

    ASSERT_EQ(input->size(), result->size());
    const auto v = ColumnHelper::as_column<NullableColumn>(result);

    EXPECT_EQ(v->get(0).get_slice(), "");
    EXPECT_EQ(v->get(1).get_slice(), "abc");
    EXPECT_EQ(v->get(2).get_slice(), "");
    EXPECT_EQ(v->get(3).get_slice(), "Invalid ? and ");
}

TEST(CelonisStringFunctionsStringSplitTest, All) {
    auto string = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto pattern = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto index = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);

    std::vector<DatumStruct> test_input = {
            // Return the first split after splitting on ','.
            {"äö,ü,abc",                        ",",   0,  "äö"},
            // Return the first split after splitting on the multi-character pattern ', '.
            {"FirstName, MiddleName, LastName", ", ",  0,  "FirstName"},
            {"Date, Notes",                     ", ",  0,  "Date"},
            {"",                                ", ",  0,  ""},
            {kNullDatum,                        ", ",  0,  kNullDatum},
            {", ",                              ", ",  0,  ""},
            {", abcd, ",                        ", ",  0,  ""},
            {", , ",                            ", ",  0,  ""},
            // Return the second split after splitting on the single-character pattern '-'.
            {"Customer-X",                      "-",   1,  "X"},
            {"Customer-Y",                      "-",   1,  "Y"},
            // Return the second split after splitting on the multi-character pattern ', '.
            {"FirstName, MiddleName, LastName", ", ",  1,  "MiddleName"},
            {"Date, Notes",                     ", ",  1,  "Notes"},
            {"",                                ", ",  1,  kNullDatum},
            {kNullDatum,                        ", ",  1,  kNullDatum},
            {", ",                              ", ",  1,  ""},
            {", abcd, ",                        ", ",  1,  "abcd"},
            {", , ",                            ", ",  1,  ""},
            // Extract the second character from the input using an empty pattern string.
            {"FirstName, LastName",             "",    1,  "i"},
            {"äö,ü,",                           "",    1,  "ö"},
            {"abcd",                            "",    1,  "b"},
            {"",                                "",    1,  ""},
            {kNullDatum,                        "",    1,  kNullDatum},
            // Return from the end of the input using a negative index.
            // Multi-character pattern
            {"FirstName, LastName",             ", ",  -1, "LastName"},
            {"FirstName, LastName",             ", ",  -2, "FirstName"},
            {"FirstName, LastName",             ", ",  -3, kNullDatum},
            {"Query",                           ", ",  -1, "Query"},
            {"Query",                           ", ",  -2, kNullDatum},
            {kNullDatum,                        ", ",  -1, kNullDatum},
            {kNullDatum,                        ", ",  -2, kNullDatum},
            // Single-character pattern
            {"FirstName,LastName",              ",",   -1, "LastName"},
            {"FirstName,LastName",              ",",   -2, "FirstName"},
            {"FirstName,LastName",              ",",   -3, kNullDatum},
            {"Query",                           ",",   -1, "Query"},
            {"Query",                           ",",   -2, kNullDatum},
            {kNullDatum,                        ",",   -1, kNullDatum},
            {kNullDatum,                        ",",   -2, kNullDatum},
            // Empty pattern
            {"äö",                              "",    -1, "ö"},
            {"äö",                              "",    -2, "ä"},
            {"äö",                              "",    -3, kNullDatum},
            {kNullDatum,                        "",    -1, kNullDatum},
            // Return the entire string if pattern does not exist in the string and split-index is zero.
            {"",                                ", ",  0,  ""},
            {"abc",                             ", ",  0,  "abc"},
            {"abc",                             ",",   0,  "abc"},
            // pattern is identical to input-string and split-index is either zero or one: An empty string is returned.
            {"",                                "",    0,  ""},
            {"",                                "",    1,  ""},
            {"",                                "",    2,  kNullDatum},
            {"a",                               "a",   0,  ""},
            {"a",                               "a",   1,  ""},
            {"a",                               "a",   2,  kNullDatum},
            {"abc",                             "abc", 0,  ""},
            {"abc",                             "abc", 1,  ""},
            {"abc",                             "abc", 2,  kNullDatum}
    };
    for (const auto& st: test_input) {
        if (st[0].is_null()) {
            string->append_nulls(1);
        } else {
            string->append_datum(st[0]);
        }
        pattern->append_datum(st[1]);
        index->append_datum(st[2]);
    }

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    const auto result = CelonisStringFunctions::string_split(ctx.get(), {string, pattern, index}).value();

    ASSERT_EQ(test_input.size(), result->size());
    const auto v = ColumnHelper::as_column<NullableColumn>(result);
    ASSERT_TRUE(v->has_null());
    for (int i = 0; i < v->size(); ++i) {
        auto debug_string = [&]() {
            return fmt::format("case: {}, string: '{}', pattern: '{}', index: {}", i,
                               test_input[i][0].is_null() ? "NULL" : test_input[i][0].get_slice(),
                               test_input[i][1].get_slice(), test_input[i][2].get_int32());
        };
        if (test_input[i][3].is_null()) {
            EXPECT_TRUE(v->is_null(i)) << debug_string();
        } else if (v->is_null(i)) {
            EXPECT_FALSE(v->is_null(i)) << debug_string();
        } else {
            EXPECT_EQ(v->get(i).get_slice(), test_input[i][3].get_slice()) << debug_string();
        }
    }
}

TEST(CelonisStringFunctionsStringToIntTest, OutOfRange) {
    auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    strings->append_datum("9223372036854775908");
    strings->append_datum("-9223372036854775809");
    strings->append_datum("123");
    const auto result = CelonisStringFunctions::string_to_int(nullptr, {strings}).value();
    ASSERT_EQ(strings->size(), result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(123L, result->get(2).get_int64());
}

TEST(CelonisStringFunctionsStringToIntTest, All) {
    auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto expected_int = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);

    std::vector<DatumStruct> test_input = {
            {"123456",              123456L},
            {"-123456.11",          -123456L},
            {"123456.11",           123456L},
            {"123456.99",           123456L},
            {"12345699",            12345699L},
            {"9223372036854775807", 9223372036854775807L},
            {"-9223372036854775808", INT64_MIN},
            // Invalid string inputs
            {kNullDatum,            kNullDatum},
            {"  123456  ",          kNullDatum},
            {"123 ",                kNullDatum},
            {" 123",                kNullDatum},
            {"4.70E+2",             kNullDatum},
            {"-5.93E-2",            kNullDatum},
            {"4.70e+2",             kNullDatum},
            {"-5.93e-2",            kNullDatum},
            {"HELLO",               kNullDatum},
    };
    for (const auto& st: test_input) {
        if (st[0].is_null()) {
            strings->append_nulls(1);
        } else {
            strings->append_datum(st[0]);
        }
        if (st[1].is_null()) {
            expected_int->append_nulls(1);
        } else {
            expected_int->append_datum(st[1]);
        }
    }

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    const auto result = CelonisStringFunctions::string_to_int(ctx.get(), {strings}).value();
    ASSERT_EQ(test_input.size(), result->size());
    const auto v = ColumnHelper::as_column<NullableColumn>(result);
    for (int i = 0; i < v->size(); ++i) {
        auto debug_string = [&]() {
            return fmt::format("case: {}, string: '{}'", i,
                               test_input[i][0].is_null() ? "NULL" : test_input[i][0].get_slice());
        };
        if (test_input[i][1].is_null()) {
            EXPECT_TRUE(v->is_null(i)) << debug_string();
        } else if (v->is_null(i)) {
            EXPECT_FALSE(v->is_null(i)) << debug_string();
        } else {
            EXPECT_EQ(v->get(i).get_int64(), test_input[i][1].get_int64()) << debug_string();
        }
    }
}

TEST(CelonisStringFunctionsStringToDoubleTest, All) {
    auto string = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto expected_double = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);

    std::vector<DatumStruct> test_input = {
            // Fixed point notation
            {"123",           123.0},
            {"  123   ",      123.0},
            {" \t 123 \n  ",  123.0},
            {"+123456",       123456.0},
            {" +123456",      123456.0},
            {" +123456 \n",   123456.0},
            {"-123456",       -123456.0},
            {"+00003",        3.0},
            {"3.",            3.0},
            {"1.11",          1.11},
            {"-9.99",         -9.99},
            {"2,500.10",      2500.1},
            {"-2,500.10",     -2500.1},
            {"1.02",          1.02},
            {"-2.1",          -2.1},
            {"45.2",          45.2},
            {"   45.2",       45.2},
            {"45.2   ",       45.2},
            {"   45.2   ",    45.2},
            {"\t\t45.2   ",   45.2},
            {"\t\t45.2 \n ",  45.2},
            // Scientific E notation
            {"4,000.0e2",     400000.0},
            {"  4,000.0e2  ", 400000.0},
            {"\t4,000.0e2  ", 400000.0},
            {"4000.0e2",      400000.0},
            {"-5.93E-2",      -0.0593},
            {"-5.93e-2",      -0.0593},
            {"  -5.93e-2  ",  -0.0593},
            {"2e0",           2.0},
            {"2e+00",         2.0},
            {"\n2e+00\n",     2.0},
            // Invalid string inputs
            {kNullDatum,      kNullDatum},
            {"",              kNullDatum},
            {"F10.0",         kNullDatum},
            {"10.F0",         kNullDatum},
            {"10.0F",         kNullDatum},
            {"3 21",          kNullDatum},
            {"1E650",         kNullDatum},
            {"++1",           kNullDatum},
            {"--1",           kNullDatum},
            {"1E",            kNullDatum},
            {"1EA",           kNullDatum},
            {"1.0.0",         kNullDatum},
            {"10,00,000",     kNullDatum},
            {"10,0000,000",   kNullDatum},
            {"1.234,5",       kNullDatum},
            {"INF",           kNullDatum},
            {"NaN",           kNullDatum},
    };
    for (const auto& st: test_input) {
        if (st[0].is_null()) {
            string->append_nulls(1);
        } else {
            string->append_datum(st[0]);
        }
        if (st[1].is_null()) {
            expected_double->append_nulls(1);
        } else {
            expected_double->append_datum(st[1]);
        }
    }

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    // Get the global locale
    std::locale pre_locale;
    const auto result = CelonisStringFunctions::string_to_double(ctx.get(), {string}).value();
    std::locale post_locale;
    // Verify that global locale is not changed by string_to_double.
    EXPECT_EQ(pre_locale, post_locale);
    ASSERT_EQ(test_input.size(), result->size());
    const auto v = ColumnHelper::as_column<NullableColumn>(result);
    for (int i = 0; i < v->size(); ++i) {
        auto debug_string = [&]() {
            return fmt::format("case: {}, string: '{}'", i,
                               test_input[i][0].is_null() ? "NULL" : test_input[i][0].get_slice());
        };
        if (test_input[i][1].is_null()) {
            EXPECT_TRUE(v->is_null(i)) << debug_string();
        } else if (v->is_null(i)) {
            EXPECT_FALSE(v->is_null(i)) << debug_string();
        } else {
            EXPECT_EQ(v->get(i).get_double(), test_input[i][1].get_double()) << debug_string();
        }
    }
}

TEST_F(CelonisStringFunctionsTest, upper) {
    // normal chars
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("Shirt");
        strings->append_datum(kNullDatum);
        strings->append_datum("Pants");
        strings->append_datum("0123456789");
        strings->append_datum("");
        strings->append_datum(kNullDatum);
        strings->append_datum("abcdefghijklmnopqrstuvwxyz");
        strings->append_datum("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        strings->append_datum("abcABCabc");
        strings->append_datum(kNullDatum);
        strings->append_datum("()**==");
        const auto result = CelonisStringFunctions::upper(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
        EXPECT_EQ("SHIRT", result->get(0).get_slice());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ("PANTS", result->get(2).get_slice());
        EXPECT_EQ("0123456789", result->get(3).get_slice());
        EXPECT_EQ("", result->get(4).get_slice());
        EXPECT_TRUE(result->get(5).is_null());
        EXPECT_EQ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", result->get(6).get_slice());
        EXPECT_EQ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", result->get(7).get_slice());
        EXPECT_EQ("ABCABCABC", result->get(8).get_slice());
        EXPECT_TRUE(result->get(9).is_null());
        EXPECT_EQ("()**==", result->get(10).get_slice());
    }
    // german chars and other special chars
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("ä");
        strings->append_datum("ö");
        strings->append_datum("ü");
        strings->append_datum("äöü");
        strings->append_datum("Ä");
        strings->append_datum("Ö");
        strings->append_datum("Ü");
        strings->append_datum(kNullDatum);
        strings->append_datum("ÄÖÜ");
        strings->append_datum("Aäöüabc");
        strings->append_datum("€☺abcäöü");

        const auto result = CelonisStringFunctions::upper(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
        EXPECT_EQ("Ä", result->get(0).get_slice());
        EXPECT_EQ("Ö", result->get(1).get_slice());
        EXPECT_EQ("Ü", result->get(2).get_slice());
        EXPECT_EQ("ÄÖÜ", result->get(3).get_slice());
        EXPECT_EQ("Ä", result->get(4).get_slice());
        EXPECT_EQ("Ö", result->get(5).get_slice());
        EXPECT_EQ("Ü", result->get(6).get_slice());
        EXPECT_TRUE(result->get(7).is_null());
        EXPECT_EQ("ÄÖÜ", result->get(8).get_slice());
        EXPECT_EQ("AÄÖÜABC", result->get(9).get_slice());
        EXPECT_EQ("€☺ABCÄÖÜ", result->get(10).get_slice());
    }
    // malformed UTF-8
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("\xC3");
        strings->append_datum("\xC3\xA4");
        const auto result = CelonisStringFunctions::upper(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
        ASSERT_EQ("\xC3", result->get(0).get_slice());
        ASSERT_EQ("Ä", result->get(1).get_slice());
    }
    // empty input column
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        const auto result = CelonisStringFunctions::upper(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
    }
}

TEST_F(CelonisStringFunctionsTest, lower) {
    // normal chars
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("Shirt");
        strings->append_datum(kNullDatum);
        strings->append_datum("Pants");
        strings->append_datum("0123456789");
        strings->append_datum("");
        strings->append_datum(kNullDatum);
        strings->append_datum("abcdefghijklmnopqrstuvwxyz");
        strings->append_datum("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        strings->append_datum("abcABCabc");
        strings->append_datum(kNullDatum);
        strings->append_datum("()**==");
        const auto result = CelonisStringFunctions::lower(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
        EXPECT_EQ("shirt", result->get(0).get_slice());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ("pants", result->get(2).get_slice());
        EXPECT_EQ("0123456789", result->get(3).get_slice());
        EXPECT_EQ("", result->get(4).get_slice());
        EXPECT_TRUE(result->get(5).is_null());
        EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", result->get(6).get_slice());
        EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", result->get(7).get_slice());
        EXPECT_EQ("abcabcabc", result->get(8).get_slice());
        EXPECT_TRUE(result->get(9).is_null());
        EXPECT_EQ("()**==", result->get(10).get_slice());
    }
    // german chars and other special chars
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("ä");
        strings->append_datum("ö");
        strings->append_datum("ü");
        strings->append_datum("äöü");
        strings->append_datum("Ä");
        strings->append_datum("Ö");
        strings->append_datum("Ü");
        strings->append_datum(kNullDatum);
        strings->append_datum("ÄÖÜ");
        strings->append_datum("Aäöüabc");
        strings->append_datum("€☺abcäöü");

        const auto result = CelonisStringFunctions::lower(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
        EXPECT_EQ("ä", result->get(0).get_slice());
        EXPECT_EQ("ö", result->get(1).get_slice());
        EXPECT_EQ("ü", result->get(2).get_slice());
        EXPECT_EQ("äöü", result->get(3).get_slice());
        EXPECT_EQ("ä", result->get(4).get_slice());
        EXPECT_EQ("ö", result->get(5).get_slice());
        EXPECT_EQ("ü", result->get(6).get_slice());
        EXPECT_TRUE(result->get(7).is_null());
        EXPECT_EQ("äöü", result->get(8).get_slice());
        EXPECT_EQ("aäöüabc", result->get(9).get_slice());
        EXPECT_EQ("€☺abcäöü", result->get(10).get_slice());
    }
    // malformed UTF-8
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        strings->append_datum("\xC3");
        strings->append_datum("\xC3\x84");
        const auto result = CelonisStringFunctions::lower(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
        ASSERT_EQ("\xC3", result->get(0).get_slice());
        ASSERT_EQ("ä", result->get(1).get_slice());
    }
    // empty input column
    {
        auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        const auto result = CelonisStringFunctions::lower(nullptr, {strings}).value();
        ASSERT_EQ(strings->size(), result->size());
    }
}

} // namespace starrocks
