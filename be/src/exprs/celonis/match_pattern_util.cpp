#include "exprs/celonis/match_pattern_util.h"

#include <gtest/gtest.h>

#include <boost/locale/utf.hpp>

// The implementation is copied from Saola: https://github.com/celonis/cpm-query-engine/blob/main/query-engine/src/main/native/cpm-accelerator/modules/cube/operator_compare/match_pattern.cpp
namespace starrocks::celonis {

namespace {

template <typename ITERATOR>
[[nodiscard]] constexpr size_t count_repeated_escape_chars(ITERATOR begin, const ITERATOR end) {
    size_t count{0};
    while (begin != end && *begin == ESCAPE_CHAR) {
        ++begin;
        ++count;
    }
    return count;
}

[[nodiscard]] std::string_view make_string_view(const char* const begin, const char* const end) {
    return {begin, static_cast<size_t>(std::distance(begin, end))};
}

/**
 * Return a substring which potentially removes a trailing escape char which is not escaped.
 * Examples:
 * "abc" --> "abc"
 * "abc\" --> "abc"
 * "abc\\" --> "abc\\"
 * "abc\\\" --> "abc\\"
 * "abc\\\\" --> "abc\\\\"
 * ...
 */
[[nodiscard]] std::string_view trim_non_escaped_trailing_escape_char(const std::string_view str) {
    if (str.empty()) {
        return str;
    }
    if (str.back() == ESCAPE_CHAR && count_repeated_escape_chars(std::next(str.rbegin()), str.rend()) % 2 == 0) {
        return str.substr(0, str.size() - 1);
    }
    return str;
}

bool is_first_utf8_byte(const unsigned char value) {
    return boost::locale::utf::utf_traits<unsigned char>::is_lead(value);
}

/**
 * Return the number of bytes of the next utf8 character based on the first
 * byte.
 * UTF-8 Character can consist out of 1, 2, 3 or 4 bytes.
 *
 * @param value First byte of the next UTF-8 character.
 * @return Number of bytes the next character has based on \a value being the
 * first byte of this character.
 */
[[nodiscard]] constexpr uint8_t get_utf8_offset(const unsigned char value) noexcept {
    // Even if the input is invalid, we still return 1 to prevent us from skipping the null byte
    constexpr uint8_t INVALID = 1;

    // Copied from the boost implementation
    // https://github.com/boostorg/locale/blob/ccb8fbb9a1a0dbdffb1054ffa34e4aba1e425642/include/boost/locale/utf.hpp#L147
    if (value < 128) {
        // Optimized for ASCII
        return 1;
    }
    if (value < 194) {
        return INVALID;
    }
    if (value < 224) {
        return 2;
    }
    if (value < 240) {
        return 3;
    }
    if (value <= 244) {
        return 4;
    }
    return INVALID;
}

bool is_valid_utf8(std::string_view input) noexcept {
    for (const auto* itr{input.begin()}; itr != input.end();) {
        const auto decoded{boost::locale::utf::utf_traits<char>::decode(itr, input.end())};
        if (decoded == boost::locale::utf::illegal || decoded == boost::locale::utf::incomplete) {
            return false;
        }
    }
    return true;
}

/**
 * Returns the number of UTF-8 character code points in a string view.
 */
[[nodiscard]] inline size_t count_code_points(std::string_view input) {
    assert(is_valid_utf8(input));

    size_t len{0};
    for (const auto* itr{input.begin()}; itr != input.end(); ++itr) {
        len += static_cast<size_t>(is_first_utf8_byte(*itr));
    }
    return len;
}

/**
 * @brief Returns the length of a pattern string, not counting escape characters
 * @param pattern
 * @return pattern length
 */
[[nodiscard]] size_t get_pattern_length(const char* const pattern) noexcept {
    const char* curr_patt_addr{pattern};
    size_t length{0};
    char curr_patt_byte{*curr_patt_addr};

    while (curr_patt_byte != '\0') {
        curr_patt_byte = *curr_patt_addr;
        ++curr_patt_addr;
        if (curr_patt_byte != '\\') {
            ++length;
        }
    }
    return length;
}
} // namespace

namespace details {
/**
 * This implements the ends_with step of the match_pattern algorithm.
 * The behavior and input/output is analogous to match_pattern_starts_with.
 */
std::optional<matching_position> match_pattern_ends_with(const std::string_view input, const std::string_view pattern) {
    auto input_itr{input.rbegin()};
    auto pattern_itr{pattern.rbegin()};
    for (; input_itr != input.rend() && pattern_itr != pattern.rend(); ++pattern_itr, ++input_itr) {
        const bool escape{count_repeated_escape_chars(std::next(pattern_itr), pattern.rend()) % 2 != 0};

        const auto input_char{*input_itr};
        const auto pattern_char{*pattern_itr};

        if (escape) {
            if (pattern_char != input_char) {
                return std::nullopt;
            }
            ++pattern_itr;
            continue;
        }

        switch (pattern_char) {
        case MATCH_ONE:
            while (!is_first_utf8_byte(*input_itr)) {
                ++input_itr;
            }
            continue;
        case MATCH_ANY:
            return matching_position{input_itr.base(), pattern_itr.base()};
        default:
            if (pattern_char != input_char) {
                return std::nullopt;
            }
            break;
        }
    }

    if (input_itr == input.rend()) {
        if (pattern_itr == pattern.rend() || *pattern_itr == MATCH_ANY) {
            return matching_position{input_itr.base(), pattern_itr.base()};
        }
        return std::nullopt;
    }
    return matching_position{input_itr.base(), pattern_itr.base()};
}

/**
 * This function implements the starts_with step of the pattern matching algorithm. It checks if the first characters
 * of the pattern (until a % is found or the pattern is depleted) appear in that order in the input. Escaped
 * characters in the pattern are taken into account.
 *
 * If the input doesn't start with the characters from the pattern, std::nullopt is returned.
 *
 * If the input starts with the characters from the pattern, an optional of a matching position is returned. This
 * contains iterators to the input and pattern which have been consumed.
 *
 * Example: Input "abcXXXXdef" and pattern "abc%def"
 * Output: matching position with an iterator to the first X of the input and an iterator to the % of the pattern
 *
 * Example: Input "XXXXdef" and pattern "abc%def"
 * Output: empty optional
 */
std::optional<matching_position> match_pattern_starts_with(const std::string_view input,
                                                           const std::string_view pattern) {
    const auto* input_itr{input.begin()};
    const auto* pattern_itr{pattern.begin()};
    for (; input_itr != input.end() && pattern_itr != pattern.end(); ++pattern_itr, ++input_itr) {
        const auto input_char{*input_itr};
        auto pattern_char{*pattern_itr};

        switch (pattern_char) {
        case MATCH_ONE:
            input_itr += get_utf8_offset(input_char) - 1;
            continue;
        case MATCH_ANY:
            return matching_position{input_itr, pattern_itr};
        case ESCAPE_CHAR: {
            const auto escaped_pattern_char{*(++pattern_itr)};
            pattern_char = escaped_pattern_char;
            [[fallthrough]];
        }
        default:
            if (pattern_char != input_char) {
                return std::nullopt;
            }
            break;
        }
    }

    if (input_itr == input.end()) {
        if (pattern_itr == pattern.end() || *pattern_itr == MATCH_ANY) {
            return matching_position{input_itr, pattern_itr};
        }
        return std::nullopt;
    }
    return matching_position{input_itr, pattern_itr};
}

/**
 * This function implements the "contains" step of the match_pattern algorithm. It checks if all character sequences
 * between 2 surrounding % signs are contained in the input in that order. The input must be trimmed to the sequences
 * which should be matched.
 * Example:
 * Input "abcXXXXqrs" and pattern "abc%def%ghijkl%%mnop%qrs"
 *           |   |                     |                |
 *   input begin end           pattern begin            end
 *
 * Input should be "XXXX" and pattern should be "def%ghijkl%%mnop%".
 *
 * This function returns a starting position of the match if all character sequences appear in the input,
 * otherwise std::nullopt.
 */
std::optional<matching_position> match_pattern_contains(const std::string_view input, const std::string_view pattern) {
    if (pattern.begin() == pattern.end()) {
        return matching_position{input.begin(), pattern.begin()};
    }

    const char* match_start{nullptr};
    for (const auto *pattern_itr{pattern.begin()}, *input_itr{input.begin()}; pattern_itr != pattern.end();) {
        const auto pattern_substring{make_string_view(pattern_itr, pattern.end())};
        const auto input_substring{make_string_view(input_itr, input.end())};

        if (const auto match{match_pattern_starts_with(input_substring, pattern_substring)}) {
            match_start = match_start == nullptr ? input_substring.begin() : match_start;
            pattern_itr = std::next(match->pattern_itr);
            input_itr = match->input_itr;
        } else {
            if (input_itr == input.end()) {
                // Reached the end of the input and could not find the current pattern substring
                return std::nullopt;
            }
            ++input_itr;
        }
    }

    return matching_position{match_start, pattern.begin()};
}

/**
 * This function returns a matching start index in the input or std::nullopt if they do not match.
 */
std::optional<const char*> find_match_start(const std::string_view input, const std::string_view pattern_sv) {
    // The underlying idea of this algorithm is as follows. Consider the following example pattern:
    //
    // Example: abc%def%ghijkl%%mnop%qrs
    //          ^  ^                ^^
    //          |  |                ||
    //          |  -- 3. contains  --|
    //          |                    |
    //    1. starts_with         2. ends_with
    //
    // One important observation is that our pattern matching does not need to be implemented recursively. You only
    // need to perform a 3. step process:
    // 1. "starts_with": check that the input starts with the characters *before* the first %
    //    ---> In the example: input must start with "abc"
    // 2. "ends_with": check that the input ends with the characters *after* the last %
    //    ---> In the example: input must end with "qrs"
    // 3. "contains": check that all character sequences *within 2 % signs* appear in the input in that order
    //    ---> In the example: After the "abc" in the input and before the "qrs" at the end of the input, find "def"
    //         followed by "ghijkl" followed by "" (empty string because 2 % signs directly next to each other) followed
    //         by "mnop"
    //
    // This is the general idea of the algorithm, the rest of the complexity simply comes from UTF-8 handling and
    // escaping.

    // Simply ignore trailing escape char
    const auto pattern{trim_non_escaped_trailing_escape_char(pattern_sv)};

    if (pattern.empty()) {
        // Empty pattern matches everything
        return input.begin();
    }

    // Step 1: Check if input starts with the characters before the first %
    const auto starts_with_match{match_pattern_starts_with(input, pattern)};
    if (!starts_with_match) {
        return std::nullopt;
    }
    if (starts_with_match->pattern_itr == pattern.end()) {
        // There was no non-escaped wildcard in the pattern
        if (starts_with_match->input_itr == input.end()) {
            return input.begin();
        }
        return std::nullopt;
    }

    const auto* contains_step_begin{std::next(starts_with_match->pattern_itr)};
    while (contains_step_begin != pattern.end() && *contains_step_begin == MATCH_ANY) {
        std::advance(contains_step_begin, 1);
    }

    if (contains_step_begin == pattern.end()) {
        // the last character was `%` which matches everything
        return input.begin();
    }

    // Step 2: Check if the input ends with the characters after the last %
    const auto ends_with_match{
            match_pattern_ends_with(make_string_view(starts_with_match->input_itr, input.end()), pattern)};
    if (!ends_with_match) {
        return std::nullopt;
    }

    std::optional<matching_position> matching_position{std::nullopt};
    if (contains_step_begin == ends_with_match->pattern_itr) {
        matching_position = details::matching_position{ends_with_match->input_itr, contains_step_begin};
    } else {
        // Step 3: "Contains step" - Check that all character sequences within 2 % signs appear in the input in that order
        matching_position =
                match_pattern_contains(make_string_view(starts_with_match->input_itr, ends_with_match->input_itr),
                                       make_string_view(contains_step_begin, ends_with_match->pattern_itr));
    }
    if (!matching_position.has_value() || matching_position->input_itr == nullptr) {
        return std::nullopt;
    }
    if (*pattern.begin() != MATCH_ANY) {
        return input.begin();
    }
    return matching_position->input_itr;
}
} // namespace details

/**
 * Find a matching index of the given pattern against the given input.
 * Both inputs must be null-terminated strings.
 * The pattern may contain wildcard characters:
 * - The underscore ('_') matches exactly one character.
 * - The percent sign ('%') matches any string with zero or more characters.
 * The wildcards can be escaped using the backslash ('\') for matching the literal wildcard character.
 *
 * This function will *always* perform pattern matching, no matter how the pattern looks like. If you wish to implement
 * behavior similar to PATINDEX, this function should NOT be called directly. PATINDEX behaves differently depending on
 * the amount of wildcards being used, which is not the case for this function.
 *
 * @return index if the pattern matches or 0
 */
size_t pattern_index(const std::string_view input, const std::string_view pattern_sv) {
    if (auto match_index{details::find_match_start(input, pattern_sv)}) {
        auto diff{count_code_points(input) - count_code_points(*match_index)};
        return diff + 1;
    }
    return 0;
}

int64_t pattern_index(const char* const text, const char* const pattern, const int64_t occurrence) {
    size_t last_found_offset{0};
    int64_t current_occurrence{0};
    const char* textBookmarkAddr{text};

    // if pattern is the empty string: pattern found (in accordance with MS SQL server)
    if (get_pattern_length(pattern) == 0) {
        return 1;
    }

    if (occurrence == 0) {
        return 1;
    }
    if (occurrence < 0) {
        // Negative occurrences never exist, therefore return 0.
        return 0;
    }

    do {
        last_found_offset = pattern_index(textBookmarkAddr, pattern);
        if (last_found_offset != 0) {
            ++current_occurrence;
        } else {
            return 0;
        }

        bool patternConditionFulfilled{(current_occurrence == occurrence)};

        if (!patternConditionFulfilled) {
            // patternIndex is 1-indexed, so if a match is found, lastFoundOffset is at least 1
            textBookmarkAddr += last_found_offset;
        } else {
            return (textBookmarkAddr + last_found_offset) - text;
        }
    } while (true);
}

bool match_pattern(const std::string_view input, const pattern_t pattern_sv) {
    return details::find_match_start(input, pattern_sv.get()) != std::nullopt;
}

} // namespace starrocks::celonis
