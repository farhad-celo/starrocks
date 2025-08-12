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

TEST(CelonisStringFunctionsStringSplitTest, All) {
    auto string  = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto pattern = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto index = ColumnHelper::create_column(TypeDescriptor(TYPE_INT), true);

    std::vector<DatumStruct> test_input = {
            // Return the first split after splitting on ','.
            {"äö,ü,abc",                        ",",   0, "äö"},
            // Return the first split after splitting on the multi-character pattern ', '.
            {"FirstName, MiddleName, LastName", ", ",  0, "FirstName"},
            {"Date, Notes",                     ", ",  0, "Date"},
            {"",                                ", ",  0, ""},
            {kNullDatum,                        ", ",  0, kNullDatum},
            {", ",                              ", ",  0, ""},
            {", abcd, ",                        ", ",  0, ""},
            {", , ",                            ", ",  0, ""},
            // Return the second split after splitting on the single-character pattern '-'.
            {"Customer-X",                      "-",   1, "X"},
            {"Customer-Y",                      "-",   1, "Y"},
            // Return the second split after splitting on the multi-character pattern ', '.
            {"FirstName, MiddleName, LastName", ", ",  1, "MiddleName"},
            {"Date, Notes",                     ", ",  1, "Notes"},
            {"",                                ", ",  1, kNullDatum},
            {kNullDatum,                        ", ",  1, kNullDatum},
            {", ",                              ", ",  1, ""},
            {", abcd, ",                        ", ",  1, "abcd"},
            {", , ",                            ", ",  1, ""},
            // Extract the second character from the input using an empty pattern string.
            {"FirstName, LastName",             "",    1, "i"},
            {"äö,ü,",                           "",    1, "ö"},
            {"abcd",                            "",    1, "b"},
            {"",                                "",    1, ""},
            {kNullDatum,                        "",    1, kNullDatum},
            // Return from the end of the input using a negative index.
            // Multi-character pattern
            {"FirstName, LastName",             ", ", -1, "LastName"},
            {"FirstName, LastName",             ", ", -2, "FirstName"},
            {"FirstName, LastName",             ", ", -3, kNullDatum},
            {"Query",                           ", ", -1, "Query"},
            {"Query",                           ", ", -2, kNullDatum},
            {kNullDatum,                        ", ", -1, kNullDatum},
            {kNullDatum,                        ", ", -2, kNullDatum},
            // Single-character pattern
            {"FirstName,LastName",              ",",  -1, "LastName"},
            {"FirstName,LastName",              ",",  -2, "FirstName"},
            {"FirstName,LastName",              ",",  -3, kNullDatum},
            {"Query",                           ",",  -1, "Query"},
            {"Query",                           ",",  -2, kNullDatum},
            {kNullDatum,                        ",",  -1, kNullDatum},
            {kNullDatum,                        ",",  -2, kNullDatum},
            // Empty pattern
            {"äö",                              "",   -1, "ö"},
            {"äö",                              "",   -2, "ä"},
            {"äö",                              "",   -3, kNullDatum},
            {kNullDatum,                        "",   -1, kNullDatum},
            // Return the entire string if pattern does not exist in the string and split-index is zero.
            {"",                                ", ",  0, ""},
            {"abc",                             ", ",  0, "abc"},
            {"abc",                             ",",   0, "abc"},
            // pattern is identical to input-string and split-index is either zero or one: An empty string is returned.
            {"",                                "",    0, ""},
            {"",                                "",    1, ""},
            {"",                                "",    2, kNullDatum},
            {"a",                               "a",   0, ""},
            {"a",                               "a",   1, ""},
            {"a",                               "a",   2, kNullDatum},
            {"abc",                             "abc", 0, ""},
            {"abc",                             "abc", 1, ""},
            {"abc",                             "abc", 2, kNullDatum}
    };
    for (const auto& st : test_input) {
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

TEST(CelonisStringFunctionsStringToDoubleTest, All) {
    auto string  = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    auto expected_double = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);

    std::vector<DatumStruct> test_input = {
        // Fixed point notation
        {"123",                        123.0},
        {"+123456",                    123456.0},
        {"-123456",                    -123456.0},
        {"+00003",                     3.0},
        {"3.",                         3.0},
        {"1.11",                       1.11},
        {"-9.99",                      -9.99},
        {"2,500.10",                   2500.1},
        {"-2,500.10",                  -2500.1},
        {"1.02",                       1.02},
        {"-2.1",                       -2.1},
        {"45.2",                       45.2},
        // Scientific E notation
        {"4,000.0e2",                  400000.0},
        {"4000.0e2",                   400000.0},
        {"-5.93E-2",                   -0.0593},
        {"-5.93e-2",                   -0.0593},
        {"2e0",                        2.0},
        {"2e+00",                      2.0},
        // Invalid string inputs
        {kNullDatum,                   kNullDatum},
        {"",                           kNullDatum},
        {"F10.0",                      kNullDatum},
        {"10.F0",                      kNullDatum},
        {"10.0F",                      kNullDatum},
        {"3 21",                       kNullDatum},
        {"1E650",                      kNullDatum},
        {"++1",                        kNullDatum},
        {"--1",                        kNullDatum},
        {"1E",                         kNullDatum},
        {"1EA",                        kNullDatum},
        {"1.0.0",                      kNullDatum},
        {"10,00,000",                  kNullDatum},
        {"10,0000,000",                kNullDatum},
        {"1.234,5",                    kNullDatum},
        {"INF",                        kNullDatum},
        {"NaN",                        kNullDatum},
    };
    for (const auto& st : test_input) {
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
    const auto result = CelonisStringFunctions::string_to_double(ctx.get(), {string}).value();
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

} // namespace starrocks
