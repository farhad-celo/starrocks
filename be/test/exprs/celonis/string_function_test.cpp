#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/vectorized_fwd.h"
#include "exprs/celonis/string_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

class CelonisStringFunctionsTranslateTest : public testing::Test {
protected:
    void translate(Columns columns, const std::vector<std::string> & res) {
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
        auto context = ctx.get();
        context->set_constant_columns(columns);

        ASSERT_TRUE(
                CelonisStringFunctions::translate_prepare(context, FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

        const auto result = CelonisStringFunctions::translate(context, columns).value();
        const auto v = ColumnHelper::as_column<BinaryColumn>(result);

        for (int i = 0; i < res.size(); ++i) {
            EXPECT_EQ(res[i], v->get_data()[i].to_string());
        }

        ASSERT_TRUE(
                CelonisStringFunctions::translate_close(context, FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                        .ok());
    }
};

TEST_F(CelonisStringFunctionsTranslateTest, nullTest) {
    Columns columns;

    auto str = BinaryColumn::create();
    str->append("dummy");
    str->append("007");

    auto nulls = NullColumn ::create();
    nulls->append(1);
    nulls->append(0);

    columns.emplace_back(NullableColumn::create(str, nulls));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("0", 1));
    columns.emplace_back(ColumnHelper::create_const_column<TYPE_VARCHAR>("a", 1));

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto context = ctx.get();
    context->set_constant_columns(columns);

    ASSERT_TRUE(
            CelonisStringFunctions::translate_prepare(context, FunctionContext::FunctionStateScope::FRAGMENT_LOCAL).ok());

    const auto result = CelonisStringFunctions::translate(context, columns).value();
    const auto v = ColumnHelper::as_column<NullableColumn>(result);
    ASSERT_TRUE(v->has_null());
    ASSERT_EQ(2, v->size());
    ASSERT_TRUE(v->is_null(0));

    ASSERT_FALSE(v->is_null(1));
    EXPECT_EQ("aa7", v->get(1).get_slice());

    ASSERT_TRUE(
            CelonisStringFunctions::translate_close(context, FunctionContext::FunctionContext::FunctionStateScope::FRAGMENT_LOCAL)
                    .ok());
}

TEST_F(CelonisStringFunctionsTranslateTest, singleCharTest) {
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

TEST_F(CelonisStringFunctionsTranslateTest, singleCharUtf8Test) {
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

TEST_F(CelonisStringFunctionsTranslateTest, multiCharSymbolTest) {
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

TEST_F(CelonisStringFunctionsTranslateTest, multiCharCharSymbolCombinationTest) {
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

TEST_F(CelonisStringFunctionsTranslateTest, multiCharSymbolCharCombinationTest) {
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

TEST_F(CelonisStringFunctionsTranslateTest, multiCharDigit2CharTest) {
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

TEST_F(CelonisStringFunctionsTranslateTest, lowerUtf8Test) {
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

TEST_F(CelonisStringFunctionsTranslateTest, upperUtf8Test) {
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

    std::vector<std::string> input_strs {
            "\0"s,
            "abc\0def"s,
            "\0abc\0def"s,
            "Invalid \xFF and \0 valid str"s
    };

    auto input = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    for (const auto& str : input_strs) {
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

} // namespace starrocks
