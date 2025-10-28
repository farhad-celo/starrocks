#include "exprs/celonis/match_pattern_util.h"

#include <gtest/gtest.h>

namespace starrocks::celonis {

[[nodiscard]] constexpr pattern_t operator""_pattern(const char* const str, size_t len) noexcept {
    return pattern_t{std::string_view{str, len}};
}

TEST(MatchPatternUtilTest, MatchAll) {
    ASSERT_TRUE(match_pattern("abc", ""_pattern));
    ASSERT_TRUE(match_pattern("", ""_pattern));
    ASSERT_TRUE(match_pattern("abc", "%"_pattern));
    ASSERT_TRUE(match_pattern("", "%"_pattern));
    ASSERT_TRUE(match_pattern("%%%", "%"_pattern));
    ASSERT_TRUE(match_pattern("", "%%%%"_pattern));
    ASSERT_TRUE(match_pattern("abc", "%%%%"_pattern));
    ASSERT_TRUE(match_pattern("%", "%%%%"_pattern));
}

TEST(MatchPatternUtilTest, Wildcards1) {
    ASSERT_TRUE(match_pattern("0abc", "_abc"_pattern));
    ASSERT_TRUE(match_pattern("0abc00", "_abc%"_pattern));
    ASSERT_TRUE(match_pattern("_0abc", "\\_%abc"_pattern));
    ASSERT_TRUE(match_pattern("_0abc00", "\\_%abc%"_pattern));
    ASSERT_TRUE(match_pattern("00abc", "%_abc"_pattern));
    ASSERT_TRUE(match_pattern("00abc00", "%_abc%"_pattern));
    ASSERT_TRUE(match_pattern("00a0bc", "%a_bc"_pattern));
    ASSERT_TRUE(match_pattern("00a0bc00", "%a_bc%"_pattern));
    ASSERT_TRUE(match_pattern("00a00bc00", "%a%bc%"_pattern));
    ASSERT_TRUE(match_pattern("00abc0", "%abc_"_pattern));
    ASSERT_TRUE(match_pattern("00abc00", "%abc_%"_pattern));
    ASSERT_TRUE(match_pattern("00abc000", "%abc%_"_pattern));
    ASSERT_TRUE(match_pattern("00abc00_", "%abc%\\_"_pattern));
    ASSERT_TRUE(match_pattern("a0bc", "a_bc"_pattern));
    ASSERT_TRUE(match_pattern("a0bc00", "a_bc%"_pattern));
    ASSERT_TRUE(match_pattern("abc0", "abc_"_pattern));
    ASSERT_TRUE(match_pattern("abc000", "abc_%"_pattern));
    ASSERT_TRUE(match_pattern("abc0", "abc%_"_pattern));
    ASSERT_TRUE(match_pattern("abc00_", "abc%\\_"_pattern));
    ASSERT_TRUE(match_pattern("a%bc", "a\\%bc"_pattern));
    ASSERT_TRUE(match_pattern("%abc", "\\%abc"_pattern));
    ASSERT_TRUE(match_pattern("abc%", "abc\\%"_pattern));
    ASSERT_TRUE(match_pattern("a_bc", "a\\_bc"_pattern));
    ASSERT_TRUE(match_pattern("_abc", "\\_abc"_pattern));
    ASSERT_TRUE(match_pattern("abc_", "abc\\_"_pattern));
}

TEST(MatchPatternUtilTest, Wildcards2) {
    ASSERT_TRUE(match_pattern("abcccd", "%ccd"_pattern));
    ASSERT_TRUE(match_pattern("mississipissippi", "%issip%ss%"_pattern));
    ASSERT_FALSE(match_pattern("xxxx%zzzzzzzzy%f", "xxxx%zzy%fffff"_pattern));
    ASSERT_TRUE(match_pattern("xxxx%zzzzzzzzy%f", "xxx%zzy%f"_pattern));
    ASSERT_FALSE(match_pattern("xxxxzzzzzzzzyf", "xxxx%zzy%fffff"_pattern));
    ASSERT_TRUE(match_pattern("xxxxzzzzzzzzyf", "xxxx%zzy%f"_pattern));
    ASSERT_TRUE(match_pattern("xyxyxyzyxyz", "xy%z%xyz"_pattern));
    ASSERT_TRUE(match_pattern("mississippi", "%sip%"_pattern));
    ASSERT_TRUE(match_pattern("xyxyxyxyz", "xy%xyz"_pattern));
    ASSERT_TRUE(match_pattern("mississippi", "mi%sip%"_pattern));
    ASSERT_TRUE(match_pattern("ababac", "%abac%"_pattern));
    ASSERT_TRUE(match_pattern("ababac", "%abac%"_pattern));
    ASSERT_TRUE(match_pattern("aaazz", "a%zz%"_pattern));
    ASSERT_FALSE(match_pattern("a12b12", "%12%23"_pattern));
    ASSERT_FALSE(match_pattern("a12b12", "a12b"_pattern));
    ASSERT_TRUE(match_pattern("a12b12", "%12%12%"_pattern));

    ASSERT_TRUE(match_pattern("%", "%"_pattern));
    ASSERT_TRUE(match_pattern("a%abab", "a%b"_pattern));
    ASSERT_TRUE(match_pattern("a%r", "a%"_pattern));
    ASSERT_FALSE(match_pattern("a%ar", "a%aar"_pattern));

    ASSERT_TRUE(match_pattern("XYXYXYZYXYz", "XY%Z%XYz"_pattern));
    ASSERT_TRUE(match_pattern("missisSIPpi", "%SIP%"_pattern));
    ASSERT_TRUE(match_pattern("mississipPI", "%issip%PI"_pattern));
    ASSERT_TRUE(match_pattern("xyxyxyxyz", "xy%xyz"_pattern));
    ASSERT_TRUE(match_pattern("miSsissippi", "mi%sip%"_pattern));
    ASSERT_FALSE(match_pattern("miSsissippi", "mi%Sip%"_pattern));
    ASSERT_TRUE(match_pattern("abAbac", "%Abac%"_pattern));
    ASSERT_TRUE(match_pattern("abAbac", "%Abac%"_pattern));
    ASSERT_TRUE(match_pattern("aAazz", "a%zz%"_pattern));
    ASSERT_FALSE(match_pattern("A12b12", "%12%23"_pattern));
    ASSERT_TRUE(match_pattern("a12B12", "%12%12%"_pattern));
    ASSERT_TRUE(match_pattern("oWn", "%oWn%"_pattern));

    ASSERT_TRUE(match_pattern("bLah", "bLah"_pattern));
    ASSERT_FALSE(match_pattern("bLah", "bLaH"_pattern));

    ASSERT_TRUE(match_pattern("a", "%_"_pattern));
    ASSERT_TRUE(match_pattern("ab", "%_"_pattern));
    ASSERT_TRUE(match_pattern("abc", "%_"_pattern));

    ASSERT_FALSE(match_pattern("a", "__"_pattern));
    ASSERT_TRUE(match_pattern("ab", "_%_"_pattern));
    ASSERT_TRUE(match_pattern("ab", "%_%_%"_pattern));
    ASSERT_TRUE(match_pattern("abc", "_%%_%_"_pattern));
    ASSERT_FALSE(match_pattern("abc", "_%%_%&_"_pattern));
    ASSERT_TRUE(match_pattern("abcd", "_b%__"_pattern));
    ASSERT_FALSE(match_pattern("abcd", "_a%__"_pattern));
    ASSERT_TRUE(match_pattern("abcd", "_%%_c_"_pattern));
    ASSERT_FALSE(match_pattern("abcd", "_%%_d_"_pattern));
    ASSERT_TRUE(match_pattern("abcde", "_%b%_%d%_"_pattern));

    ASSERT_TRUE(match_pattern("bLah", "bL_h"_pattern));
    ASSERT_FALSE(match_pattern("bLaaa", "bLa_"_pattern));
    ASSERT_TRUE(match_pattern("bLah", "bLa_"_pattern));
    ASSERT_FALSE(match_pattern("bLaH", "_Lah"_pattern));
    ASSERT_TRUE(match_pattern("bLaH", "_LaH"_pattern));

    ASSERT_TRUE(
            match_pattern("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab",
                          "a%a%a%a%a%a%aa%aaa%a%a%b"_pattern));
    ASSERT_TRUE(
            match_pattern("abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffaf"
                          "agaagggagaaaaaaaab",
                          "%a%b%ba%ca%a%aa%aaa%fa%ga%b%"_pattern));
    ASSERT_FALSE(
            match_pattern("abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffaf"
                          "agaagggagaaaaaaaab",
                          "%a%b%ba%ca%a%x%aaa%fa%ga%b%"_pattern));
    ASSERT_FALSE(
            match_pattern("abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffaf"
                          "agaagggagaaaaaaaab",
                          "%a%b%ba%ca%aaaa%fa%ga%gggg%b%"_pattern));
    ASSERT_TRUE(
            match_pattern("abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffaf"
                          "agaagggagaaaaaaaab",
                          "%a%b%ba%ca%aaaa%fa%ga%ggg%b%"_pattern));
    ASSERT_TRUE(match_pattern("aaabbaabbaab", "%aabbaa%a%"_pattern));
    ASSERT_TRUE(match_pattern("a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%", "a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%"_pattern));
    ASSERT_TRUE(match_pattern("aaaaaaaaaaaaaaaaa", "%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%"_pattern));
    ASSERT_FALSE(match_pattern("aaaaaaaaaaaaaaaa", "%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%a%"_pattern));
    ASSERT_FALSE(
            match_pattern("abc%abcd%abcde%abcdef%abcdefg%abcdefgh%abcdefghi%abcdefghij%abcdefghijk%abcdefghijkl%"
                          "abcdefghijklm%abcdefghijklmn",
                          "abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%"_pattern));
    ASSERT_TRUE(
            match_pattern("abc%abcd%abcde%abcdef%abcdefg%abcdefgh%abcdefghi%abcdefghij%abcdefghijk%abcdefghijkl%"
                          "abcdefghijklm%abcdefghijklmn",
                          "abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%"_pattern));
    ASSERT_FALSE(match_pattern("abc%abcd%abcd%abc%abcd", "abc%abc%abc%abc%abc"_pattern));
    ASSERT_TRUE(match_pattern("abc%abcd%abcd%abc%abcd%abcd%abc%abcd%abc%abc%abcd",
                              "abc%abc%abc%abc%abc%abc%abc%abc%abc%abc%abcd"_pattern));
    ASSERT_TRUE(match_pattern("abc", "%%%%%%%%a%%%%%%%%b%%%%%%%%c%%%%%%%%"_pattern));
    ASSERT_FALSE(match_pattern("%%%%%%%%a%%%%%%%%b%%%%%%%%c%%%%%%%%", "abc"_pattern));
    ASSERT_FALSE(match_pattern("abc", "%%%%%%%%a%%%%%%%%b%%%%%%%%b%%%%%%%%"_pattern));
    ASSERT_TRUE(match_pattern("%abc%", "%%%a%b%c%%%"_pattern));
}

TEST(MatchPatternUtilTest, Wildcards3) {
    ASSERT_TRUE(match_pattern("hawkeye", "h%"_pattern));
    ASSERT_FALSE(match_pattern("hawkeye", "H%"_pattern));
    ASSERT_FALSE(match_pattern("hawkeye", "indio%"_pattern));
    ASSERT_TRUE(match_pattern("hawkeye", "h%eye"_pattern));
    ASSERT_TRUE(match_pattern("indio", "_ndio"_pattern));
    ASSERT_TRUE(match_pattern("indio", "in__o"_pattern));
    ASSERT_FALSE(match_pattern("indio", "in_o"_pattern));

    ASSERT_TRUE(match_pattern("h%", "h\\%"_pattern));
    ASSERT_FALSE(match_pattern("h%wkeye", "h\\%"_pattern));
    ASSERT_TRUE(match_pattern("h%wkeye", "h\\%%"_pattern));
    ASSERT_TRUE(match_pattern("h%awkeye", "h\\%a%k%e"_pattern));
    ASSERT_TRUE(match_pattern("indio", "_ndio"_pattern));
    ASSERT_TRUE(match_pattern("i_dio", "i\\_d_o"_pattern));
    ASSERT_FALSE(match_pattern("i_dio", "i\\_nd_o"_pattern));
    ASSERT_TRUE(match_pattern("i_dio", "i\\_d%o"_pattern));

    ASSERT_TRUE(match_pattern("foo", "_%"_pattern));
    ASSERT_TRUE(match_pattern("f", "_%"_pattern));
    ASSERT_FALSE(match_pattern("", "_%"_pattern));
    ASSERT_TRUE(match_pattern("foo", "%_"_pattern));
    ASSERT_TRUE(match_pattern("f", "%_"_pattern));
    ASSERT_FALSE(match_pattern("", "%_"_pattern));

    ASSERT_TRUE(match_pattern("foo", "__%"_pattern));
    ASSERT_TRUE(match_pattern("foo", "___%"_pattern));
    ASSERT_FALSE(match_pattern("foo", "____%"_pattern));
    ASSERT_TRUE(match_pattern("foo", "%__"_pattern));
    ASSERT_TRUE(match_pattern("foo", "%___"_pattern));
    ASSERT_FALSE(match_pattern("foo", "%____"_pattern));

    ASSERT_TRUE(match_pattern("jack", "%____%"_pattern));

    ASSERT_TRUE(match_pattern("ABC%__ABC", "%%%%\\%\\_\\_ABC"_pattern));
    ASSERT_TRUE(match_pattern("%AB\\\\\\_C", "_AB\\\\\\\\__C"_pattern));

    ASSERT_TRUE(match_pattern("\\_", "%\\\\\\_"_pattern));
    ASSERT_FALSE(match_pattern("\\x", "%\\\\\\_"_pattern));
    ASSERT_TRUE(match_pattern("ab\\_", "%ab\\\\\\_"_pattern));
    ASSERT_FALSE(match_pattern("ab\\x", "%ab\\\\\\_"_pattern));

    ASSERT_TRUE(match_pattern("\\\\_", "%\\\\\\\\_"_pattern));
    ASSERT_TRUE(match_pattern("\\\\x", "%\\\\\\\\_"_pattern));
    ASSERT_TRUE(match_pattern("ab\\\\_", "%ab\\\\\\\\_"_pattern));
    ASSERT_TRUE(match_pattern("ab\\\\x", "%ab\\\\\\\\_"_pattern));

    // CPL-8160
    ASSERT_FALSE(match_pattern("A", "_%A%_"_pattern));
    ASSERT_FALSE(match_pattern("AAA", "___%A%___"_pattern));
    ASSERT_FALSE(match_pattern("AAA", "__%__"_pattern));
    ASSERT_TRUE(match_pattern("AAAA", "__%__"_pattern));
}

// test unicode
TEST(MatchPatternUtilTest, Unicode) {
    constexpr auto smallUnicodeInput =
            "öüäß\u3000a\u009C\u0064b\u0134\u2134c\u2268\u0468\u067Cd\u9644\u9813e\u306F\u3136abc";
    ASSERT_TRUE(match_pattern(smallUnicodeInput, "%ö%a%b%c%d%e%_"_pattern));
    ASSERT_TRUE(
            match_pattern(smallUnicodeInput, "%__ß%\u3000%\u0064b\u0134\u2134c%%%d\u9644%_%e\u306F\u3136abc"_pattern));
    ASSERT_TRUE(match_pattern(smallUnicodeInput, "%ö%"_pattern));
    ASSERT_TRUE(match_pattern(smallUnicodeInput, "%_ß%"_pattern));
    ASSERT_TRUE(
            match_pattern(smallUnicodeInput,
                          "%\u3000_\u009C\u0064_\u0134\u2134_\u2268\u0468\u067C_\u9644\u9813_\u306F\u3136abc"_pattern));
    ASSERT_TRUE(match_pattern(smallUnicodeInput,
                              "%_\u0064%\u0134\u2134c\u2268\u0468\u067C_\u9644\u9813e\u306F\u3136abc"_pattern));
    ASSERT_TRUE(match_pattern(smallUnicodeInput, "%_\u0064%\u0134\u2134c\u2268\u0468\u067C_\u9644\u9813e%b_"_pattern));
    ASSERT_TRUE(match_pattern(smallUnicodeInput, "%\u067C__\u9813%"_pattern));

    ASSERT_FALSE(match_pattern(smallUnicodeInput, "%_X_%"_pattern));
    ASSERT_FALSE(match_pattern(smallUnicodeInput, "%\u067C__\u067C%"_pattern));
    ASSERT_FALSE(match_pattern(smallUnicodeInput, "%\u2134%\u2134%"_pattern));
    ASSERT_FALSE(match_pattern(smallUnicodeInput, "%_\\_ß%"_pattern));

    ASSERT_TRUE(match_pattern("öäbü%öxüä", "__b_%_x__"_pattern));
}

TEST(MatchPatternUtilTest, SpecialCharacters) {
    ASSERT_TRUE(match_pattern("Celon\t\n\t\r\\%\\_&_asis", "C%e%l%o%n%i%s"_pattern));
    ASSERT_TRUE(match_pattern("Celäöüüöäöäöüöäöüonis", "C%e%l%o%n%i%s"_pattern));
    ASSERT_TRUE(match_pattern("Cel☭ϠϠϠϠϠϠϠϠϠϠϠ☭onis", "C%e%l%o%n%i%s"_pattern));
    ASSERT_FALSE(match_pattern("??????", "C%e%l%o%n%i%s"_pattern));
    ASSERT_FALSE(match_pattern("bla bla bla bla", "C%e%l%o%n%i%s"_pattern));
}

TEST(MatchPatternUtilTest, Celonis) {
    ASSERT_TRUE(match_pattern("Celonis", "%Celonis%"_pattern));
    ASSERT_TRUE(match_pattern("Celonis", "%Celonis%"_pattern));
    ASSERT_FALSE(match_pattern("??????", "%Celonis%"_pattern));
    ASSERT_FALSE(match_pattern("bla bla bla bla", "%Celonis%"_pattern));
}

TEST(MatchPatternUtilTest, Underscore) {
    ASSERT_TRUE(match_pattern("Cel\nonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Cel\ronis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Cel\nonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Cel\tonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Celaonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Cel_onis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("CelΣonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("CelΧonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("CelϠonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("CelϺonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Celြonis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Cel☭onis", "Cel_onis"_pattern));
    ASSERT_TRUE(match_pattern("Celäonis", "Cel_onis"_pattern));
}

TEST(MatchPatternUtilTest, Examples) {
    ASSERT_TRUE(match_pattern("Test", "Te%\\"_pattern));
    ASSERT_TRUE(match_pattern("Test\\", "Te%\\\\\\"_pattern));
    ASSERT_FALSE(match_pattern("Test", "Te%\\\\\\"_pattern));
    ASSERT_TRUE(match_pattern("mHmFHXCXuuBOiSYAkAbtldfYDdkZzykKvxTpQpOv", "%Ab_ldfYD%ZzykK_xT%"_pattern));
    ASSERT_TRUE(match_pattern("QFuUgKLqLPGlKVYVMBwzLFMPORrntjKeLKIzXvTp",
                              "%gKLq_PG%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%VMBwzLF%_Rrntj%"_pattern));
    ASSERT_TRUE(match_pattern("oGlFBdMscVBxAwrNfcNBFsXyKvXRhMGiZEtrTcHr", "oGlFB%TcHr"_pattern));
    ASSERT_TRUE(match_pattern("RHlWHItxPubzkuvAigyiLgzbEZUZPGAfsZbzKIXQ", "%Pubzkuv%AigyiLgz_EZUZPGAf%"_pattern));
    ASSERT_TRUE(match_pattern("XfGmuLPFgrrWEFmGKPkFFGIYnjWgokUmIfTkmBsV",
                              "XfGmuLPFgrrWEFmGKPkFFGIYnjWgokUmIfTkmBsV"_pattern));
    ASSERT_TRUE(match_pattern("inAvuaCUGvgYzUmsnQsUeixXGHwfiwuOSTLhJZYe", "inAvuaCUGvgYzUmsnQsU%"_pattern));
    ASSERT_TRUE(match_pattern("gtpMVENUCVpaAdHdPRFzalGekdiGYMpQPsxLWKQF",
                              "gtpMVENUCVpaAdHdPRFzalGekdiGYMpQPsxLWKQ_"_pattern));
    ASSERT_TRUE(match_pattern("f444-4742-", "%4_-%42-"_pattern));
    ASSERT_FALSE(match_pattern("yOQDuWYXBNTFkxgMJGjNORLJgklAVdXlBcYhSFmm",
                               "%yOQDuWYXBNTFkxgMJGjNORLJgklAVdXlBcYSFmm"_pattern));
    ASSERT_FALSE(
            match_pattern("VddrkYRJxHOANugdfSNfHVZCNRtKzgbFbdvibuwc",
                          "%V%d%d%r%k%Y%R%J%x%H%O%A%N%u%g%d%f%S%N%f%H%V%Z%C%N%R%t%K%z%g%b%F%b%d%v%i%b%u%w"_pattern));
    ASSERT_FALSE(match_pattern("ievJvoYSkGBimPIPXNVwdXlZvNtpbqnfaiRMduPi", "%P"_pattern));
}

TEST(MatchPatternUtilTest, EscapedWildcardAfterWildcard) {
    // See CPL-6445
    ASSERT_FALSE(match_pattern("_US_", "%\\_S_"_pattern));
    ASSERT_FALSE(match_pattern("_US_", "%\\_S_%"_pattern));
    ASSERT_FALSE(match_pattern("_US_", "%%\\_S_%"_pattern));
    ASSERT_FALSE(match_pattern("%US_", "%\\%S_%"_pattern));
    ASSERT_TRUE(match_pattern("_S_", "%\\_S_"_pattern));
    ASSERT_TRUE(match_pattern("_S_", "%\\_S_%"_pattern));
    ASSERT_TRUE(match_pattern("f_S_", "%%\\_S_%"_pattern));
    ASSERT_TRUE(match_pattern("%S_", "%\\%S_%"_pattern));
    ASSERT_FALSE(match_pattern("foo_US_bar", "%\\_S_%"_pattern));
    ASSERT_TRUE(match_pattern("_S", "%\\_S"_pattern));
    ASSERT_FALSE(match_pattern("_S", "%\\_S_"_pattern));

    ASSERT_TRUE(match_pattern("a%S", "_\\%S"_pattern));
    ASSERT_FALSE(match_pattern("abS", "_\\%S"_pattern));
    ASSERT_FALSE(match_pattern("abbbbS", "_\\%S"_pattern));
}

TEST(MatchPatternUtilTest, EscapedEscapes) {
    const auto pattern{R"(%\\%)"_pattern};
    EXPECT_FALSE(match_pattern("Take off", pattern));
    EXPECT_TRUE(match_pattern("Take\\off", pattern));
    EXPECT_TRUE(match_pattern("Take\\\\off", pattern));
    EXPECT_TRUE(match_pattern("Take\\off\\", pattern));
    EXPECT_TRUE(match_pattern("Take\\\\off\\", pattern));
}

namespace {

constexpr std::string_view NO_MATCH{};

void check_match(const std::string_view input, const std::string_view pattern, const std::string_view expected,
                 const std::optional<details::matching_position> match, bool match_start_with_utf8_char = false) {
    if (match) {
        ASSERT_NE(expected, NO_MATCH) << "Expected a match for input '" << input << "' with pattern '" << pattern
                                      << "', but none was found.";
        constexpr size_t INPUT_OFFSET{2};                             // Corresponds to R"(
        const size_t pattern_offset{INPUT_OFFSET + input.size() + 7}; // +7 corresponds to )", R"(
        /*
     * +2 corresponds when the match start with an UTF8 code points.
     * The match point is shown with `^` in the excepted, but it's an UTF8 code point in the input and the pattern.
     * Therefore, the length of the excepted string needs to be adjusted.
     */
        ASSERT_EQ(expected.size() + (match_start_with_utf8_char ? 2 : 0), pattern_offset + pattern.size() + 1)
                << "Unexpected length of the 'expected' string visualization.";
        ASSERT_EQ(input.begin() + (expected.find('^') - INPUT_OFFSET), match->input_itr)
                << "Mismatch in the resulting input iterator position.";
        ASSERT_EQ(pattern.begin() + (expected.rfind('^') - pattern_offset + (match_start_with_utf8_char ? 1 : 0)),
                  match->pattern_itr)
                << "Mismatch in the resulting pattern iterator position.";
    } else {
        ASSERT_EQ(expected, NO_MATCH) << "Expected no match for input '" << input << "' with pattern '" << pattern
                                      << "', but a match was found.";
    }
}

} // namespace

TEST(MatchPatternUtilTest, MatchPatternStartsWith) {
    using details::match_pattern_starts_with;
    constexpr auto test_starts_with = [](std::string_view input, std::string_view pattern, std::string_view expected) {
        check_match(input, pattern, expected, match_pattern_starts_with(input, pattern));
    };

    // The line below the input/pattern has indicators '^' where the output iterators are expected to be.
    test_starts_with(R"()", R"()", //
                     "  ^      ^");
    test_starts_with(R"()", R"(abc)", //
                     NO_MATCH);
    test_starts_with(R"(abc)", R"()", //
                     "  ^         ^");
    test_starts_with(R"(abc)", R"(abc)", //
                     "     ^         ^");
    test_starts_with(R"(abc123)", R"(abc)", //
                     "     ^            ^");
    test_starts_with(R"(abc)", R"(abc%)", //
                     "     ^         ^ ");
    test_starts_with(R"(abc123)", R"(abc%)", //
                     "     ^            ^ ");
    test_starts_with(R"(abc123)", R"(a_c%)", //
                     "     ^            ^ ");
    test_starts_with(R"(axc123)", R"(a_c%%)", //
                     "     ^            ^  ");
    test_starts_with(R"(abc)", R"(%)", //
                     "  ^         ^ ");
    test_starts_with(R"(a\c123)", R"(a\\c%)", //
                     "     ^             ^ ");
    test_starts_with(R"(abc123)", R"(a\\c%)", //
                     NO_MATCH);
    test_starts_with(R"(a_c123)", R"(a\_c%)", //
                     "     ^             ^ ");
    test_starts_with(R"(abc123)", R"(a\_c%)", //
                     NO_MATCH);
    test_starts_with(R"(a\_c123)", R"(a\\_c%)", //
                     "      ^              ^ ");
    test_starts_with(R"(a\xc123)", R"(a\\_c%)", //
                     "      ^              ^ ");
    test_starts_with(R"(abc)", R"(foo)", //
                     NO_MATCH);
    test_starts_with(R"(abc)", R"(abx%)", //
                     NO_MATCH);
    test_starts_with(R"(aäc123)", R"(a_c%)", //
                     "   ä ^            ^ ");
    test_starts_with(R"(äöü123)", R"(ä_ü%)", //
                     "  äöü^         ä ü^ ");
}

TEST(MatchPatternUtilTest, MatchPatternEndsWith) {
    using details::match_pattern_ends_with;
    constexpr auto test_ends_with = [](std::string_view input, std::string_view pattern, std::string_view expected) {
        check_match(input, pattern, expected, match_pattern_ends_with(input, pattern));
    };

    // The line below the input/pattern has indicators '^' where the output iterators are expected to be.
    test_ends_with(R"()", R"()", //
                   "  ^      ^");
    test_ends_with(R"()", R"(abc)", //
                   NO_MATCH);
    test_ends_with(R"(abc)", R"()", //
                   "     ^      ^");
    test_ends_with(R"(abc)", R"(abc)", //
                   "  ^         ^   ");
    test_ends_with(R"(123abc)", R"(abc)", //
                   "     ^         ^   ");
    test_ends_with(R"(abc)", R"(%abc)", //
                   "  ^          ^   ");
    test_ends_with(R"(123abc)", R"(%abc)", //
                   "     ^          ^   ");
    test_ends_with(R"(123abc)", R"(%a_c)", //
                   "     ^          ^   ");
    test_ends_with(R"(123axc)", R"(%%a_c)", //
                   "     ^           ^   ");
    test_ends_with(R"(abc)", R"(%)", //
                   "     ^       ^");
    test_ends_with(R"(123a\c)", R"(%a\\c)", //
                   "     ^          ^    ");
    test_ends_with(R"(123abc)", R"(%a\\c)", //
                   NO_MATCH);
    test_ends_with(R"(123a_c)", R"(%a\_c)", //
                   "     ^          ^    ");
    test_ends_with(R"(123abc)", R"(%a\_c)", //
                   NO_MATCH);
    test_ends_with(R"(123a\_c)", R"(%a\\_c)", //
                   "     ^           ^     ");
    test_ends_with(R"(123a\xc)", R"(%a\\_c)", //
                   "     ^           ^     ");
    test_ends_with(R"(abc)", R"(foo)", //
                   NO_MATCH);
    test_ends_with(R"(abc)", R"(%xbc)", //
                   NO_MATCH);
    test_ends_with(R"(123aäc)", R"(%a_c)", //
                   "     ^ä         ^   ");
    test_ends_with(R"(123aäöü)", R"(%aä_ü)", //
                   "     ^äöü        ^ä ü ");
}

TEST(MatchPatternUtilTest, MatchPatternContains) {
    constexpr auto test_contains_with = [](std::string_view input, std::string_view pattern, std::string_view expected,
                                           bool match_start_with_utf8_char = false) {
        ASSERT_FALSE(pattern.empty());
        ASSERT_EQ(pattern.front(), '%'); // Using the raw character instead of MATCH_ANY for clarity
        ASSERT_EQ(pattern.back(), '%');
        // Need to call substr here because the contract for contains is that the pattern is preceded by a %
        check_match(input, pattern, expected, details::match_pattern_contains(input, pattern.substr(1)),
                    match_start_with_utf8_char);
    };

    test_contains_with(R"(abc)", R"(%)", //
                       "  ^          ^");
    test_contains_with(R"(abc)", R"(%abc%)", //
                       "  ^          ^    ");
    test_contains_with(R"(xyz)", R"(%abc%)", //
                       NO_MATCH);
    test_contains_with(R"(ab)", R"(%abc%)", //
                       NO_MATCH);
    test_contains_with(R"(ab%)", R"(%abc%)", //
                       NO_MATCH);
    test_contains_with(R"(01234abc5678)", R"(%abc%)", //
                       "       ^              ^    ");
    test_contains_with(R"(abc)", R"(%a%b%c%)", //
                       "  ^          ^      ");
    test_contains_with(R"(abc)", R"(%ab%c%)", //
                       "  ^          ^     ");
    test_contains_with(R"(abc)", R"(%a%bc%)", //
                       "  ^          ^     ");
    test_contains_with(R"(abc)", R"(%_%_%_%)", //
                       "  ^          ^      ");
    test_contains_with(R"(123)", R"(%_%_%_%)", //
                       "  ^          ^      ");
    test_contains_with(R"(123)", R"(%\_%__%)", //
                       NO_MATCH);
    test_contains_with(R"(_23)", R"(%\_%__%)", //
                       "  ^          ^      ");
    test_contains_with(R"(%23)", R"(%\%%__%)", //
                       "  ^          ^      ");
    test_contains_with(R"(_23)", R"(%\%%__%)", //
                       NO_MATCH);
    test_contains_with(R"(%23)", R"(%\\%%__%)", //
                       NO_MATCH);
    test_contains_with(R"(\23)", R"(%\\%%__%)", //
                       "  ^          ^       ");
    test_contains_with(R"()", R"(%%%%)", //
                       "  ^       ^   ");
    test_contains_with(R"(xyzab)", R"(%ab%%%)", //
                       "     ^         ^     ");
    test_contains_with(R"(xyzabxyz)", R"(%ab%%%)", //
                       "     ^            ^     ");
    test_contains_with(R"(aääbäääbbababbabccc)", R"(%äää%abc%)", //
                       "   ää ^ää                    ^ää      ", true);
    test_contains_with(R"(aääbäääbbababbabacc)", R"(%äää%abc%)", //
                       NO_MATCH);
    test_contains_with(R"(aääbäaäbbababbabccc)", R"(%äää%abc%)", //
                       NO_MATCH);
}

} // namespace starrocks::celonis